/**
 * @file httpCSIReceiver.js
 * @brief HTTP-only CSI data receiver for simplified Docker deployment
 * Alternative to MQTT-based communication for direct ESP32 > Docker server
 */

import express from 'express';
import { EventEmitter } from 'events';
import csiDatabase from './csiDatabase.js';
import logger from '../utils/logger.js';

class HTTPCSIReceiver extends EventEmitter {
    constructor() {
        super();
        this.router = express.Router();
        this.setupRoutes();
        this.processingQueue = [];
        this.isProcessing = false;
        
        logger.info('HTTP CSI Receiver initialized - MQTT-free architecture');
    }

    /**
     * Setup HTTP routes for CSI data reception
     */
    setupRoutes() {
        // CSI Data endpoint - replaces MQTT csi/+/data
        this.router.post('/csi/:nodeId/data', async (req, res) => {
            try {
                const { nodeId } = req.params;
                const csiData = req.body;
                
                await this.handleCSIData(nodeId, csiData);
                
                res.json({ 
                    success: true, 
                    nodeId, 
                    timestamp: Date.now(),
                    message: 'CSI data received' 
                });
            } catch (error) {
                logger.error(`HTTP CSI data error for ${req.params.nodeId}:`, error);
                res.status(500).json({ 
                    success: false, 
                    error: error.message 
                });
            }
        });

        // Node stats endpoint - replaces MQTT csi/+/stats
        this.router.post('/csi/:nodeId/stats', async (req, res) => {
            try {
                const { nodeId } = req.params;
                const stats = req.body;
                
                await this.updateNodeStats(nodeId, stats);
                
                res.json({ 
                    success: true, 
                    nodeId, 
                    stats 
                });
            } catch (error) {
                logger.error(`HTTP stats error for ${req.params.nodeId}:`, error);
                res.status(500).json({ 
                    success: false, 
                    error: error.message 
                });
            }
        });

        // Node heartbeat endpoint - replaces MQTT csi/+/heartbeat
        this.router.post('/csi/:nodeId/heartbeat', async (req, res) => {
            try {
                const { nodeId } = req.params;
                const heartbeat = req.body;
                
                await this.handleNodeHeartbeat(nodeId, heartbeat);
                
                res.json({ 
                    success: true, 
                    nodeId, 
                    heartbeat: 'received' 
                });
            } catch (error) {
                logger.error(`HTTP heartbeat error for ${req.params.nodeId}:`, error);
                res.status(500).json({ 
                    success: false, 
                    error: error.message 
                });
            }
        });

        // Bulk data endpoint for efficiency
        this.router.post('/csi/bulk', async (req, res) => {
            try {
                const { data } = req.body; // Array of CSI data points
                const results = [];
                
                for (const item of data) {
                    if (item.nodeId && item.csiData) {
                        await this.handleCSIData(item.nodeId, item.csiData);
                        results.push({ nodeId: item.nodeId, success: true });
                    }
                }
                
                res.json({ 
                    success: true, 
                    processed: results.length,
                    results 
                });
            } catch (error) {
                logger.error('HTTP bulk data error:', error);
                res.status(500).json({ 
                    success: false, 
                    error: error.message 
                });
            }
        });

        // OTA version check endpoint
        this.router.get('/ota/:nodeId/version', (req, res) => {
            const { nodeId } = req.params;
            
            // Return current firmware version info
            res.json({
                nodeId,
                currentVersion: '1.0.0',
                latestVersion: '1.0.1',
                updateAvailable: true,
                downloadUrl: `/api/ota/${nodeId}/firmware`,
                changelog: 'Bug fixes and performance improvements',
                mandatory: false,
                maxSize: 1310720 // 1.25MB OTA limit
            });
        });

        // OTA firmware download endpoint
        this.router.get('/ota/:nodeId/firmware', (req, res) => {
            const { nodeId } = req.params;
            
            // In production, serve actual firmware binary
            res.json({
                message: 'Firmware download endpoint',
                nodeId,
                note: 'In production, this would stream the firmware binary',
                headers: {
                    'Content-Type': 'application/octet-stream',
                    'Content-Length': '1048576', // Example size
                    'X-Firmware-Version': '1.0.1',
                    'X-SHA256': 'abcdef1234567890...' // Integrity hash
                }
            });
        });

        // OTA status update endpoint
        this.router.post('/ota/:nodeId/status', (req, res) => {
            const { nodeId } = req.params;
            const { status, version, error } = req.body;
            
            logger.info(`OTA status update from ${nodeId}: ${status}`, { version, error });
            
            res.json({ 
                success: true, 
                nodeId, 
                status: 'acknowledged' 
            });
        });

        logger.info('HTTP CSI routes configured');
    }

    /**
     * Handle incoming CSI data via HTTP (same logic as MQTT version)
     */
    async handleCSIData(nodeId, csiData) {
        try {
            const data = typeof csiData === 'string' ? JSON.parse(csiData) : csiData;
            
            data.node_id = nodeId;
            data.timestamp = data.timestamp || Date.now();

            // Store raw CSI data
            await csiDatabase.insertCSIData(data);

            // Update node last seen
            await csiDatabase.updateNodeStatus(nodeId, true);

            // Emit for real-time processing
            this.emit('csi-data', {
                nodeId,
                timestamp: data.timestamp,
                mac: data.mac_address,
                rssi: data.rssi,
                amplitude: data.amplitude_data,
                phase: data.phase_data,
                source: 'http' // Distinguish from MQTT
            });

            // Queue for position calculation
            this.queueForPositioning(data);

            logger.debug(`HTTP CSI data received from ${nodeId}: RSSI=${data.rssi}`);
        } catch (error) {
            logger.error(`Failed to process HTTP CSI data from ${nodeId}:`, error);
            throw error; // Re-throw for HTTP error response
        }
    }

    /**
     * Queue CSI data for position calculation (same as MQTT version)
     */
    queueForPositioning(data) {
        this.processingQueue.push(data);
        
        if (!this.isProcessing) {
            this.processPositioningQueue();
        }
    }

    /**
     * Process positioning queue (same logic as MQTT version)
     */
    async processPositioningQueue() {
        if (this.processingQueue.length === 0) {
            this.isProcessing = false;
            return;
        }

        this.isProcessing = true;

        try {
            const groupedData = this.groupByMac(this.processingQueue);
            this.processingQueue = [];

            for (const [mac, dataPoints] of Object.entries(groupedData)) {
                await this.calculatePosition(mac, dataPoints);
            }
        } catch (error) {
            logger.error('HTTP position calculation error:', error);
        }

        setTimeout(() => this.processPositioningQueue(), 100);
    }

    /**
     * Group CSI data by MAC address
     */
    groupByMac(dataArray) {
        return dataArray.reduce((acc, data) => {
            const mac = data.mac_address;
            if (!acc[mac]) acc[mac] = [];
            acc[mac].push(data);
            return acc;
        }, {});
    }

    /**
     * Calculate position from CSI data (same as MQTT version)
     */
    async calculatePosition(targetMac, dataPoints) {
        try {
            const csiData = await csiDatabase.getCSIDataForPositioning(targetMac, 2000);
            
            if (csiData.length < 3) return;

            const nodePositions = {};
            for (const data of csiData) {
                const nodeInfo = await csiDatabase.getNodeInfo(data.node_id);
                if (nodeInfo) {
                    nodePositions[data.node_id] = {
                        x: nodeInfo.position_x,
                        y: nodeInfo.position_y,
                        z: nodeInfo.position_z
                    };
                }
            }

            const position = this.trilateration(csiData, nodePositions);
            
            if (position) {
                await csiDatabase.saveCalculatedPosition({
                    timestamp: Date.now(),
                    target_mac: targetMac,
                    x: position.x,
                    y: position.y,
                    z: position.z || 1.0,
                    confidence: position.confidence,
                    algorithm: 'http-rssi-trilateration',
                    node_count: csiData.length,
                    processing_time_ms: position.processingTime
                });

                this.emit('position-update', {
                    mac: targetMac,
                    position,
                    timestamp: Date.now(),
                    source: 'http'
                });
            }
        } catch (error) {
            logger.error(`Failed to calculate position for ${targetMac}:`, error);
        }
    }

    /**
     * Simple trilateration algorithm (same as MQTT version)
     */
    trilateration(csiData, nodePositions) {
        const startTime = Date.now();
        
        try {
            const measurements = csiData.map(data => {
                const nodePos = nodePositions[data.node_id];
                if (!nodePos) return null;

                const distance = Math.pow(10, (data.avg_rssi + 30) / -20);

                return {
                    x: nodePos.x,
                    y: nodePos.y,
                    distance,
                    weight: 1 / Math.pow(distance, 2)
                };
            }).filter(m => m !== null);

            if (measurements.length < 3) return null;

            let sumX = 0, sumY = 0, sumWeight = 0;
            
            for (const m of measurements) {
                sumX += m.x * m.weight;
                sumY += m.y * m.weight;
                sumWeight += m.weight;
            }

            return {
                x: sumX / sumWeight,
                y: sumY / sumWeight,
                z: 1.0,
                confidence: this.calculateConfidence(measurements),
                processingTime: Date.now() - startTime
            };
        } catch (error) {
            logger.error('HTTP trilateration error:', error);
            return null;
        }
    }

    /**
     * Calculate position confidence
     */
    calculateConfidence(measurements) {
        const nodeCount = measurements.length;
        const nodeScore = Math.min(nodeCount / 5, 1.0);
        
        const distances = measurements.map(m => m.distance);
        const avgDistance = distances.reduce((a, b) => a + b, 0) / distances.length;
        const variance = distances.reduce((sum, d) => sum + Math.pow(d - avgDistance, 2), 0) / distances.length;
        const consistencyScore = 1 / (1 + variance / 10);
        
        return nodeScore * 0.5 + consistencyScore * 0.5;
    }

    /**
     * Update node statistics
     */
    async updateNodeStats(nodeId, stats) {
        try {
            // Store node statistics in database
            await csiDatabase.updateNodeStats(nodeId, {
                ...stats,
                last_update: Date.now(),
                communication_method: 'http'
            });

            logger.debug(`HTTP stats updated for ${nodeId}:`, stats);
        } catch (error) {
            logger.error(`Failed to update stats for ${nodeId}:`, error);
            throw error;
        }
    }

    /**
     * Handle node heartbeat
     */
    async handleNodeHeartbeat(nodeId, heartbeat) {
        try {
            await csiDatabase.updateNodeStatus(nodeId, true, {
                ...heartbeat,
                last_heartbeat: Date.now(),
                communication_method: 'http'
            });

            this.emit('node-heartbeat', {
                nodeId,
                heartbeat,
                timestamp: Date.now(),
                source: 'http'
            });

            logger.debug(`HTTP heartbeat received from ${nodeId}`);
        } catch (error) {
            logger.error(`Failed to handle heartbeat from ${nodeId}:`, error);
            throw error;
        }
    }

    /**
     * Get system statistics
     */
    async getStats() {
        const stats = await csiDatabase.getSystemStats();
        
        stats.queueSize = this.processingQueue.length;
        stats.isProcessing = this.isProcessing;
        stats.communicationMethod = 'http';
        stats.mqttRequired = false;
        
        return stats;
    }

    /**
     * Get Express router for HTTP endpoints
     */
    getRouter() {
        return this.router;
    }
}

// Export singleton instance
export default new HTTPCSIReceiver();