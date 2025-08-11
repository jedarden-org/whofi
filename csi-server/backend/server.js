#!/usr/bin/env node

/**
 * CSI Backend Server
 * Comprehensive backend service for WiFi CSI-based positioning system
 * 
 * Features:
 * - MQTT subscriber for CSI data
 * - WebSocket server for real-time updates
 * - REST API for configuration and data access
 * - Position calculation algorithms
 * - Data processing pipeline
 * - Integration with InfluxDB and Redis
 */

const express = require('express');
const http = require('http');
const WebSocket = require('ws');
const mqtt = require('mqtt');
const redis = require('redis');
const { InfluxDB, Point } = require('@influxdata/influxdb-client');
const cors = require('cors');
const helmet = require('helmet');
const morgan = require('morgan');
const winston = require('winston');
const { v4: uuidv4 } = require('uuid');
const _ = require('lodash');
const moment = require('moment');
const { evaluate } = require('mathjs');
const { Matrix } = require('ml-matrix');
const ss = require('simple-statistics');

require('dotenv').config();

// Configuration
const CONFIG = {
    PORT: process.env.PORT || 3000,
    WS_PORT: process.env.WS_PORT || 8080,
    MQTT: {
        HOST: process.env.MQTT_BROKER_HOST || 'localhost',
        PORT: process.env.MQTT_BROKER_PORT || 1883,
        USERNAME: process.env.MQTT_USERNAME || 'csi_user',
        PASSWORD: process.env.MQTT_PASSWORD || 'csi_password',
        CLIENT_ID: `csi-backend-${uuidv4()}`
    },
    INFLUXDB: {
        URL: process.env.INFLUXDB_URL || 'http://localhost:8086',
        TOKEN: process.env.INFLUXDB_TOKEN,
        ORG: process.env.INFLUXDB_ORG || 'csi-org',
        BUCKET: process.env.INFLUXDB_BUCKET || 'csi_data'
    },
    REDIS: {
        HOST: process.env.REDIS_HOST || 'localhost',
        PORT: process.env.REDIS_PORT || 6379,
        PASSWORD: process.env.REDIS_PASSWORD
    },
    NTP_SERVER: process.env.NTP_SERVER || 'localhost'
};

// Logger setup
const logger = winston.createLogger({
    level: 'info',
    format: winston.format.combine(
        winston.format.timestamp(),
        winston.format.errors({ stack: true }),
        winston.format.json()
    ),
    transports: [
        new winston.transports.Console({
            format: winston.format.combine(
                winston.format.colorize(),
                winston.format.simple()
            )
        })
    ]
});

// Global state
const state = {
    nodes: new Map(),
    positions: new Map(),
    csiBuffer: new Map(),
    clients: new Set(),
    isProcessing: false,
    stats: {
        packetsReceived: 0,
        positionsCalculated: 0,
        uptime: Date.now()
    }
};

class CSIBackendServer {
    constructor() {
        this.app = express();
        this.server = http.createServer(this.app);
        this.wss = new WebSocket.Server({ port: CONFIG.WS_PORT });
        
        // Initialize services
        this.initializeMiddleware();
        this.initializeRoutes();
        this.initializeWebSocket();
        this.initializeMQTT();
        this.initializeDatabase();
        this.initializeRedis();
    }

    initializeMiddleware() {
        this.app.use(helmet());
        this.app.use(cors());
        this.app.use(morgan('combined'));
        this.app.use(express.json({ limit: '10mb' }));
        this.app.use(express.urlencoded({ extended: true, limit: '10mb' }));
    }

    initializeRoutes() {
        // Health check
        this.app.get('/health', (req, res) => {
            res.json({
                status: 'healthy',
                uptime: Date.now() - state.stats.uptime,
                nodes: state.nodes.size,
                positions: state.positions.size,
                stats: state.stats
            });
        });

        // API health endpoint (direct route for backward compatibility)
        this.app.get('/api/health', (req, res) => {
            res.json({
                status: 'healthy',
                uptime: Date.now() - state.stats.uptime,
                nodes: state.nodes.size,
                positions: state.positions.size,
                stats: state.stats
            });
        });

        // API Routes
        this.app.use('/api', this.createAPIRouter());
    }

    createAPIRouter() {
        const router = express.Router();

        // Node management
        router.get('/nodes', (req, res) => {
            const nodes = Array.from(state.nodes.entries()).map(([id, node]) => ({
                id,
                ...node
            }));
            res.json({ nodes });
        });

        router.post('/nodes/:nodeId/config', (req, res) => {
            const { nodeId } = req.params;
            const config = req.body;

            this.publishNodeConfig(nodeId, config);
            res.json({ success: true, nodeId, config });
        });

        // Position data
        router.get('/positions', (req, res) => {
            const { limit = 100, from, to } = req.query;
            
            if (this.influxDB) {
                this.queryPositionHistory(from, to, limit)
                    .then(positions => res.json({ positions }))
                    .catch(err => res.status(500).json({ error: err.message }));
            } else {
                const positions = Array.from(state.positions.entries())
                    .slice(-limit)
                    .map(([timestamp, position]) => ({ timestamp, ...position }));
                res.json({ positions });
            }
        });

        router.get('/positions/current', (req, res) => {
            const latest = Array.from(state.positions.entries()).pop();
            if (latest) {
                res.json({ timestamp: latest[0], ...latest[1] });
            } else {
                res.status(404).json({ error: 'No position data available' });
            }
        });

        // CSI data analysis
        router.get('/csi/:nodeId', (req, res) => {
            const { nodeId } = req.params;
            const { limit = 100 } = req.query;

            const nodeBuffer = state.csiBuffer.get(nodeId) || [];
            const data = nodeBuffer.slice(-limit);
            
            res.json({
                nodeId,
                count: data.length,
                data: data
            });
        });

        // System configuration
        router.get('/config', (req, res) => {
            res.json({
                positioning: {
                    algorithm: 'trilateration',
                    minNodes: 3,
                    updateRate: 1000,
                    smoothing: true
                },
                nodes: this.getNodePositions(),
                calibration: {
                    pathLossExponent: 2.0,
                    referenceRSSI: -30,
                    frequency: 2400
                }
            });
        });

        router.put('/config', (req, res) => {
            const config = req.body;
            logger.info('Configuration updated', config);
            res.json({ success: true, config });
        });

        // CSI data submission endpoint (for testing)
        router.post('/csi/submit', (req, res) => {
            const { nodeId, rssi, channel, rate, csi_data } = req.body;
            
            if (!nodeId) {
                return res.status(400).json({ error: 'nodeId is required' });
            }
            
            const mockData = {
                rssi: rssi || -50,
                channel: channel || 1,
                rate: rate || 54,
                csi_data: csi_data || Array.from({ length: 52 }, () => Math.random() * 100)
            };
            
            this.processCSIData(nodeId, mockData);
            
            res.json({
                success: true,
                message: 'CSI data received',
                nodeId,
                timestamp: Date.now()
            });
        });

        // Statistics
        router.get('/stats', (req, res) => {
            res.json({
                ...state.stats,
                nodes: {
                    total: state.nodes.size,
                    active: Array.from(state.nodes.values())
                        .filter(node => node.lastSeen > Date.now() - 30000).length
                },
                buffer: {
                    totalEntries: Array.from(state.csiBuffer.values())
                        .reduce((sum, buffer) => sum + buffer.length, 0)
                },
                websocket: {
                    clients: state.clients.size
                }
            });
        });

        return router;
    }

    initializeWebSocket() {
        this.wss.on('connection', (ws, req) => {
            const clientId = uuidv4();
            state.clients.add(ws);
            
            logger.info(`WebSocket client connected: ${clientId}`);

            ws.on('message', (message) => {
                try {
                    const data = JSON.parse(message);
                    this.handleWebSocketMessage(ws, data);
                } catch (error) {
                    logger.error('WebSocket message error:', error);
                }
            });

            ws.on('close', () => {
                state.clients.delete(ws);
                logger.info(`WebSocket client disconnected: ${clientId}`);
            });

            // Send initial data
            ws.send(JSON.stringify({
                type: 'welcome',
                clientId,
                nodes: Array.from(state.nodes.keys()),
                latestPosition: Array.from(state.positions.entries()).pop()
            }));
        });
    }

    handleWebSocketMessage(ws, data) {
        switch (data.type) {
            case 'subscribe':
                // Handle subscription to specific data types
                break;
            case 'unsubscribe':
                // Handle unsubscription
                break;
            default:
                logger.warn('Unknown WebSocket message type:', data.type);
        }
    }

    initializeMQTT() {
        const mqttUrl = `mqtt://${CONFIG.MQTT.HOST}:${CONFIG.MQTT.PORT}`;
        
        this.mqttClient = mqtt.connect(mqttUrl, {
            clientId: CONFIG.MQTT.CLIENT_ID,
            username: CONFIG.MQTT.USERNAME,
            password: CONFIG.MQTT.PASSWORD,
            clean: true,
            reconnectPeriod: 5000
        });

        this.mqttClient.on('connect', () => {
            logger.info('Connected to MQTT broker');
            
            // Subscribe to CSI data topics
            const topics = [
                'csi/+/data',
                'csi/+/stats',
                'csi/+/heartbeat'
            ];

            topics.forEach(topic => {
                this.mqttClient.subscribe(topic, (err) => {
                    if (err) {
                        logger.error(`Failed to subscribe to ${topic}:`, err);
                    } else {
                        logger.info(`Subscribed to ${topic}`);
                    }
                });
            });
        });

        this.mqttClient.on('message', (topic, message) => {
            this.handleMQTTMessage(topic, message);
        });

        this.mqttClient.on('error', (error) => {
            logger.error('MQTT error:', error);
        });

        this.mqttClient.on('offline', () => {
            logger.warn('MQTT client offline');
        });
    }

    handleMQTTMessage(topic, message) {
        try {
            const parts = topic.split('/');
            if (parts.length < 3) return;

            const nodeId = parts[1];
            const messageType = parts[2];
            const payload = JSON.parse(message.toString());

            state.stats.packetsReceived++;

            switch (messageType) {
                case 'data':
                    this.processCSIData(nodeId, payload);
                    break;
                case 'stats':
                    this.updateNodeStats(nodeId, payload);
                    break;
                case 'heartbeat':
                    this.handleNodeHeartbeat(nodeId, payload);
                    break;
                default:
                    logger.debug(`Unknown message type: ${messageType}`);
            }
        } catch (error) {
            logger.error('Error handling MQTT message:', error);
        }
    }

    processCSIData(nodeId, data) {
        const timestamp = Date.now();
        
        // Store in buffer
        if (!state.csiBuffer.has(nodeId)) {
            state.csiBuffer.set(nodeId, []);
        }

        const buffer = state.csiBuffer.get(nodeId);
        buffer.push({
            timestamp,
            ...data
        });

        // Keep only last 1000 entries per node
        if (buffer.length > 1000) {
            buffer.shift();
        }

        // Update node info
        const node = state.nodes.get(nodeId) || {};
        state.nodes.set(nodeId, {
            ...node,
            lastSeen: timestamp,
            lastRSSI: data.rssi,
            packetCount: (node.packetCount || 0) + 1
        });

        // Store in InfluxDB
        if (this.influxDB) {
            this.writeCSIDataToInflux(nodeId, data, timestamp);
        }

        // Calculate position if we have enough nodes
        if (state.csiBuffer.size >= 3 && !state.isProcessing) {
            this.calculatePosition();
        }

        // Broadcast to WebSocket clients
        this.broadcastToClients({
            type: 'csi_data',
            nodeId,
            data: { timestamp, ...data }
        });
    }

    async calculatePosition() {
        state.isProcessing = true;

        try {
            const nodePositions = this.getNodePositions();
            const distances = {};

            // Calculate distances from RSSI
            for (const [nodeId, buffer] of state.csiBuffer.entries()) {
                if (buffer.length === 0) continue;
                
                // Use recent samples for stability
                const recentSamples = buffer.slice(-10);
                const avgRSSI = ss.mean(recentSamples.map(s => s.rssi));
                
                // Convert RSSI to distance using path loss model
                const distance = this.rssiToDistance(avgRSSI);
                distances[nodeId] = distance;
            }

            if (Object.keys(distances).length >= 3) {
                const position = await this.trilaterate(distances, nodePositions);
                
                if (position && this.isValidPosition(position)) {
                    const timestamp = Date.now();
                    
                    // Apply smoothing
                    const smoothedPosition = this.smoothPosition(position);
                    
                    state.positions.set(timestamp, smoothedPosition);
                    state.stats.positionsCalculated++;

                    // Store in InfluxDB
                    if (this.influxDB) {
                        this.writePositionToInflux(smoothedPosition, timestamp);
                    }

                    // Cache in Redis
                    if (this.redisClient) {
                        await this.redisClient.setEx(
                            'latest_position',
                            300, // 5 minutes TTL
                            JSON.stringify({ timestamp, ...smoothedPosition })
                        );
                    }

                    // Broadcast position update
                    this.broadcastToClients({
                        type: 'position_update',
                        position: { timestamp, ...smoothedPosition }
                    });

                    logger.info('Position calculated:', smoothedPosition);
                }
            }
        } catch (error) {
            logger.error('Error calculating position:', error);
        } finally {
            state.isProcessing = false;
        }
    }

    rssiToDistance(rssi, txPower = -30, pathLossExponent = 2.0) {
        // Path loss model: d = 10^((P_tx - RSSI) / (10 * n))
        const distance = Math.pow(10, (txPower - rssi) / (10 * pathLossExponent));
        return Math.max(0.1, Math.min(distance, 100)); // Clamp to reasonable range
    }

    async trilaterate(distances, nodePositions) {
        try {
            const nodes = Object.keys(distances).slice(0, 3);
            if (nodes.length < 3) return null;

            const positions = nodes.map(nodeId => nodePositions[nodeId]).filter(Boolean);
            if (positions.length < 3) return null;

            // Trilateration using least squares
            const A = new Matrix(2, 2);
            const b = new Matrix(2, 1);

            const p1 = positions[0];
            const p2 = positions[1];
            const p3 = positions[2];

            const r1 = distances[nodes[0]];
            const r2 = distances[nodes[1]];
            const r3 = distances[nodes[2]];

            // Set up linear system
            A.set(0, 0, 2 * (p2.x - p1.x));
            A.set(0, 1, 2 * (p2.y - p1.y));
            A.set(1, 0, 2 * (p3.x - p2.x));
            A.set(1, 1, 2 * (p3.y - p2.y));

            b.set(0, 0, Math.pow(r1, 2) - Math.pow(r2, 2) - Math.pow(p1.x, 2) + Math.pow(p2.x, 2) - Math.pow(p1.y, 2) + Math.pow(p2.y, 2));
            b.set(1, 0, Math.pow(r2, 2) - Math.pow(r3, 2) - Math.pow(p2.x, 2) + Math.pow(p3.x, 2) - Math.pow(p2.y, 2) + Math.pow(p3.y, 2));

            // Solve Ax = b
            const solution = A.inverse().mmul(b);

            return {
                x: solution.get(0, 0),
                y: solution.get(1, 0),
                confidence: this.calculateConfidence(distances, positions)
            };
        } catch (error) {
            logger.error('Trilateration error:', error);
            return null;
        }
    }

    calculateConfidence(distances, positions) {
        // Simple confidence based on distance consistency
        const distanceVariance = ss.variance(Object.values(distances));
        const confidence = Math.max(0, Math.min(1, 1 - (distanceVariance / 100)));
        return confidence;
    }

    isValidPosition(position) {
        // Validate position is within reasonable bounds
        return position.x >= -100 && position.x <= 100 &&
               position.y >= -100 && position.y <= 100 &&
               !isNaN(position.x) && !isNaN(position.y);
    }

    smoothPosition(newPosition) {
        const positions = Array.from(state.positions.values());
        if (positions.length === 0) return newPosition;

        // Simple moving average for smoothing
        const recent = positions.slice(-5);
        const avgX = ss.mean([...recent.map(p => p.x), newPosition.x]);
        const avgY = ss.mean([...recent.map(p => p.y), newPosition.y]);

        return {
            ...newPosition,
            x: avgX,
            y: avgY
        };
    }

    getNodePositions() {
        // Default node positions - should be configurable
        return {
            'esp32_001': { x: 0, y: 0 },
            'esp32_002': { x: 10, y: 0 },
            'esp32_003': { x: 5, y: 10 },
            'esp32_004': { x: 0, y: 10 }
        };
    }

    updateNodeStats(nodeId, stats) {
        const node = state.nodes.get(nodeId) || {};
        state.nodes.set(nodeId, {
            ...node,
            ...stats,
            lastSeen: Date.now()
        });
    }

    handleNodeHeartbeat(nodeId, heartbeat) {
        const node = state.nodes.get(nodeId) || {};
        state.nodes.set(nodeId, {
            ...node,
            lastHeartbeat: Date.now(),
            uptime: heartbeat.uptime,
            memoryFree: heartbeat.memoryFree
        });
    }

    publishNodeConfig(nodeId, config) {
        const topic = `csi/${nodeId}/config`;
        this.mqttClient.publish(topic, JSON.stringify(config));
        logger.info(`Published config to ${nodeId}:`, config);
    }

    broadcastToClients(message) {
        const data = JSON.stringify(message);
        state.clients.forEach(client => {
            if (client.readyState === WebSocket.OPEN) {
                client.send(data);
            }
        });
    }

    async initializeDatabase() {
        if (CONFIG.INFLUXDB.TOKEN) {
            this.influxDB = new InfluxDB({
                url: CONFIG.INFLUXDB.URL,
                token: CONFIG.INFLUXDB.TOKEN
            });

            this.writeAPI = this.influxDB.getWriteApi(
                CONFIG.INFLUXDB.ORG,
                CONFIG.INFLUXDB.BUCKET
            );

            this.queryAPI = this.influxDB.getQueryApi(CONFIG.INFLUXDB.ORG);

            logger.info('InfluxDB initialized');
        }
    }

    async initializeRedis() {
        try {
            this.redisClient = redis.createClient({
                socket: {
                    host: CONFIG.REDIS.HOST,
                    port: CONFIG.REDIS.PORT
                },
                password: CONFIG.REDIS.PASSWORD
            });

            await this.redisClient.connect();
            logger.info('Redis connected');
        } catch (error) {
            logger.error('Redis connection failed:', error);
        }
    }

    writeCSIDataToInflux(nodeId, data, timestamp) {
        if (!this.writeAPI) return;

        const point = new Point('csi_data')
            .tag('node_id', nodeId)
            .floatField('rssi', data.rssi)
            .intField('channel', data.channel || 0)
            .floatField('rate', data.rate || 0)
            .timestamp(new Date(timestamp));

        if (data.csi_data && Array.isArray(data.csi_data)) {
            // Store CSI amplitude data
            data.csi_data.forEach((value, index) => {
                if (typeof value === 'number') {
                    point.floatField(`csi_${index}`, value);
                }
            });
        }

        this.writeAPI.writePoint(point);
    }

    writePositionToInflux(position, timestamp) {
        if (!this.writeAPI) return;

        const point = new Point('position')
            .floatField('x', position.x)
            .floatField('y', position.y)
            .floatField('confidence', position.confidence || 0)
            .timestamp(new Date(timestamp));

        this.writeAPI.writePoint(point);
    }

    async queryPositionHistory(from, to, limit = 100) {
        if (!this.queryAPI) return [];

        const query = `
            from(bucket: "${CONFIG.INFLUXDB.BUCKET}")
                |> range(start: ${from || '-1h'}, stop: ${to || 'now()'})
                |> filter(fn: (r) => r._measurement == "position")
                |> limit(n: ${limit})
        `;

        const result = [];
        await this.queryAPI.queryRows(query, {
            next: (row, tableMeta) => {
                const record = tableMeta.toObject(row);
                result.push(record);
            }
        });

        return result;
    }

    start() {
        this.server.listen(CONFIG.PORT, () => {
            logger.info(`CSI Backend Server running on port ${CONFIG.PORT}`);
            logger.info(`WebSocket server running on port ${CONFIG.WS_PORT}`);
        });

        // Graceful shutdown
        process.on('SIGTERM', () => {
            logger.info('SIGTERM received, shutting down gracefully');
            this.server.close(() => {
                if (this.mqttClient) this.mqttClient.end();
                if (this.redisClient) this.redisClient.quit();
                process.exit(0);
            });
        });
    }
}

// Start the server
if (require.main === module) {
    const server = new CSIBackendServer();
    server.start();
}

module.exports = CSIBackendServer;