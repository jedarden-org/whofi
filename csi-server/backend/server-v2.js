#!/usr/bin/env node

/**
 * CSI Backend Server v2.0.0
 * HTTP + WebSocket architecture for ESP32-S3 CSI positioning
 * 
 * Features:
 * - HTTP REST API for commands, config, OTA
 * - WebSocket streaming for real-time CSI data
 * - No MQTT broker dependency
 * - Optimized for ESP32-S3 SuperMini deployment
 */

const express = require('express');
const http = require('http');
const WebSocket = require('ws');
const redis = require('redis');
const { InfluxDB, Point } = require('@influxdata/influxdb-client');
const cors = require('cors');
const helmet = require('helmet');
const morgan = require('morgan');
const winston = require('winston');
const { v4: uuidv4 } = require('uuid');
const compression = require('compression');

require('dotenv').config();

const VERSION = 'v2.0.0';

// Configuration
const CONFIG = {
    PORT: process.env.PORT || 3000,
    WS_PORT: process.env.WS_PORT || 8080,
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
    devices: new Map(),
    wsConnections: new Map(),
    csiBuffer: new Map(),
    stats: {
        httpRequests: 0,
        wsConnections: 0,
        csiPacketsReceived: 0,
        positionsCalculated: 0,
        uptime: Date.now()
    }
};

class CSIServerV2 {
    constructor() {
        this.app = express();
        this.server = http.createServer(this.app);
        this.wss = new WebSocket.Server({ port: CONFIG.WS_PORT });
        
        this.initializeMiddleware();
        this.initializeRoutes();
        this.initializeWebSocket();
        this.initializeDatabase();
        this.initializeRedis();
        
        logger.info(`CSI Server ${VERSION} initializing...`);
    }

    initializeMiddleware() {
        this.app.use(helmet());
        this.app.use(cors());
        this.app.use(compression());
        this.app.use(morgan('combined'));
        this.app.use(express.json({ limit: '10mb' }));
        this.app.use(express.urlencoded({ extended: true, limit: '10mb' }));
        
        // Add version header
        this.app.use((req, res, next) => {
            res.header('X-API-Version', VERSION);
            state.stats.httpRequests++;
            next();
        });
    }

    initializeRoutes() {
        // Health check
        this.app.get('/health', (req, res) => {
            res.json({
                status: 'healthy',
                version: VERSION,
                architecture: 'HTTP + WebSocket',
                uptime: Date.now() - state.stats.uptime,
                devices: state.devices.size,
                wsConnections: state.wsConnections.size,
                stats: state.stats
            });
        });

        // API routes
        this.app.use('/api', this.createAPIRouter());
    }

    createAPIRouter() {
        const router = express.Router();

        // CSI Data endpoint (HTTP POST alternative to WebSocket)
        router.post('/csi/data', (req, res) => {
            try {
                const csiData = req.body;
                this.processCsiData(csiData.device_id || 'unknown', csiData);
                res.json({ 
                    success: true, 
                    timestamp: Date.now(),
                    message: 'CSI data received' 
                });
            } catch (error) {
                logger.error('HTTP CSI data error:', error);
                res.status(500).json({ 
                    success: false, 
                    error: error.message 
                });
            }
        });

        // System metrics endpoint
        router.post('/system/metrics', (req, res) => {
            try {
                const metrics = req.body;
                this.updateDeviceMetrics(metrics.device_id, metrics);
                res.json({ success: true });
            } catch (error) {
                logger.error('HTTP metrics error:', error);
                res.status(500).json({ success: false, error: error.message });
            }
        });

        // Device heartbeat endpoint
        router.post('/device/heartbeat', (req, res) => {
            try {
                const heartbeat = req.body;
                this.updateDeviceStatus(heartbeat.device_id, heartbeat);
                res.json({ success: true });
            } catch (error) {
                logger.error('HTTP heartbeat error:', error);
                res.status(500).json({ success: false, error: error.message });
            }
        });

        // Device alert endpoint
        router.post('/device/alert', (req, res) => {
            try {
                const alert = req.body;
                this.processAlert(alert);
                res.json({ success: true });
            } catch (error) {
                logger.error('HTTP alert error:', error);
                res.status(500).json({ success: false, error: error.message });
            }
        });

        // OTA endpoints
        router.get('/ota/:deviceId/version', (req, res) => {
            const { deviceId } = req.params;
            res.json({
                device_id: deviceId,
                current_version: '1.0.0',
                latest_version: VERSION,
                update_available: true,
                download_url: `/api/ota/${deviceId}/firmware`,
                configurations: [
                    {
                        name: 'http-only',
                        size_kb: 270,
                        description: 'Maximum size optimization, HTTP polling only'
                    },
                    {
                        name: 'http-websocket', 
                        size_kb: 320,
                        description: 'Real-time WebSocket streaming + HTTP API'
                    }
                ]
            });
        });

        router.get('/ota/:deviceId/firmware', (req, res) => {
            const { deviceId } = req.params;
            const { config = 'http-websocket' } = req.query;
            
            // Serve firmware binary based on configuration
            const firmwarePath = `/app/firmware/csi-firmware-${config}-${VERSION}.bin`;
            res.download(firmwarePath, `firmware-${config}.bin`);
        });

        // Configuration endpoints
        router.get('/config/:deviceId', (req, res) => {
            const { deviceId } = req.params;
            const device = state.devices.get(deviceId);
            
            res.json({
                device_id: deviceId,
                config: device?.config || this.getDefaultConfig(),
                last_updated: device?.configUpdated || null
            });
        });

        router.post('/config/:deviceId', (req, res) => {
            const { deviceId } = req.params;
            const config = req.body;
            
            this.updateDeviceConfig(deviceId, config);
            res.json({ success: true, device_id: deviceId });
        });

        // Statistics
        router.get('/stats', (req, res) => {
            res.json({
                version: VERSION,
                architecture: 'HTTP + WebSocket',
                ...state.stats,
                devices: {
                    total: state.devices.size,
                    active: Array.from(state.devices.values())
                        .filter(device => device.lastSeen > Date.now() - 30000).length
                },
                websocket: {
                    connections: state.wsConnections.size
                }
            });
        });

        return router;
    }

    initializeWebSocket() {
        this.wss.on('connection', (ws, req) => {
            const connectionId = uuidv4();
            state.wsConnections.set(connectionId, {
                ws,
                deviceId: null,
                connected: Date.now()
            });
            state.stats.wsConnections++;
            
            logger.info(`WebSocket connected: ${connectionId}`);

            ws.on('message', (message) => {
                try {
                    const data = JSON.parse(message);
                    this.handleWebSocketMessage(connectionId, data);
                } catch (error) {
                    logger.error('WebSocket message error:', error);
                    this.sendWebSocketError(ws, 'Invalid JSON message');
                }
            });

            ws.on('close', () => {
                const connection = state.wsConnections.get(connectionId);
                if (connection && connection.deviceId) {
                    logger.info(`Device disconnected: ${connection.deviceId}`);
                }
                state.wsConnections.delete(connectionId);
                logger.info(`WebSocket disconnected: ${connectionId}`);
            });

            // Send welcome message
            this.sendWebSocketMessage(ws, {
                msg_type: 7, // PONG
                message: 'Connected to CSI Server v2.0.0',
                server_time: Date.now(),
                connection_id: connectionId
            });
        });

        logger.info(`WebSocket server listening on port ${CONFIG.WS_PORT}`);
    }

    handleWebSocketMessage(connectionId, data) {
        const connection = state.wsConnections.get(connectionId);
        if (!connection) return;

        // Associate device ID with connection
        if (data.device_id && !connection.deviceId) {
            connection.deviceId = data.device_id;
            logger.info(`Device registered: ${data.device_id}`);
        }

        switch (data.msg_type) {
            case 1: // CSI_DATA
                this.processCsiData(data.device_id, data.csi_data || data);
                break;
                
            case 2: // SYSTEM_METRICS
                this.updateDeviceMetrics(data.device_id, data.metrics || data);
                break;
                
            case 3: // HEARTBEAT
                this.updateDeviceStatus(data.device_id, data);
                break;
                
            case 4: // ALERT
                this.processAlert(data);
                break;
                
            case 5: // BATCH_CSI
                if (data.csi_batch) {
                    data.csi_batch.forEach(csiData => {
                        this.processCsiData(data.device_id, csiData);
                    });
                }
                break;
                
            case 6: // PING
                this.sendWebSocketMessage(connection.ws, {
                    msg_type: 7, // PONG
                    timestamp: Date.now(),
                    device_id: data.device_id
                });
                break;
                
            default:
                logger.warn('Unknown WebSocket message type:', data.msg_type);
        }
    }

    processCsiData(deviceId, data) {
        const timestamp = Date.now();
        
        // Store in buffer
        if (!state.csiBuffer.has(deviceId)) {
            state.csiBuffer.set(deviceId, []);
        }

        const buffer = state.csiBuffer.get(deviceId);
        buffer.push({
            timestamp,
            ...data
        });

        // Keep only last 1000 entries per device
        if (buffer.length > 1000) {
            buffer.shift();
        }

        // Update device info
        const device = state.devices.get(deviceId) || {};
        state.devices.set(deviceId, {
            ...device,
            lastSeen: timestamp,
            lastRSSI: data.rssi,
            packetCount: (device.packetCount || 0) + 1
        });

        // Store in InfluxDB
        if (this.influxDB) {
            this.writeCSIDataToInflux(deviceId, data, timestamp);
        }

        state.stats.csiPacketsReceived++;
        
        // Broadcast to other WebSocket clients if needed
        this.broadcastToWebSocketClients({
            type: 'csi_data_update',
            device_id: deviceId,
            timestamp,
            data
        });
    }

    updateDeviceMetrics(deviceId, metrics) {
        const device = state.devices.get(deviceId) || {};
        state.devices.set(deviceId, {
            ...device,
            metrics,
            metricsUpdated: Date.now(),
            lastSeen: Date.now()
        });
    }

    updateDeviceStatus(deviceId, status) {
        const device = state.devices.get(deviceId) || {};
        state.devices.set(deviceId, {
            ...device,
            status,
            statusUpdated: Date.now(),
            lastSeen: Date.now()
        });
    }

    updateDeviceConfig(deviceId, config) {
        const device = state.devices.get(deviceId) || {};
        state.devices.set(deviceId, {
            ...device,
            config,
            configUpdated: Date.now()
        });
    }

    processAlert(alert) {
        logger.warn('Device alert:', alert);
        
        // Broadcast alert to monitoring clients
        this.broadcastToWebSocketClients({
            type: 'device_alert',
            ...alert,
            server_timestamp: Date.now()
        });
    }

    sendWebSocketMessage(ws, message) {
        if (ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify(message));
        }
    }

    sendWebSocketError(ws, error) {
        this.sendWebSocketMessage(ws, {
            type: 'error',
            message: error,
            timestamp: Date.now()
        });
    }

    broadcastToWebSocketClients(message) {
        const data = JSON.stringify(message);
        state.wsConnections.forEach(connection => {
            if (connection.ws.readyState === WebSocket.OPEN) {
                connection.ws.send(data);
            }
        });
    }

    getDefaultConfig() {
        return {
            csi: {
                enabled: true,
                sample_rate: 100,
                filter_threshold: -60
            },
            http: {
                server_url: `http://${process.env.HOST || 'localhost'}:${CONFIG.PORT}`,
                timeout_ms: 5000
            },
            websocket: {
                enabled: true,
                server_url: `ws://${process.env.HOST || 'localhost'}:${CONFIG.WS_PORT}/ws/csi-stream`,
                auto_reconnect: true
            },
            ota: {
                enabled: true,
                check_interval: 3600
            }
        };
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

    writeCSIDataToInflux(deviceId, data, timestamp) {
        if (!this.writeAPI) return;

        const point = new Point('csi_data')
            .tag('device_id', deviceId)
            .floatField('rssi', data.rssi || 0)
            .intField('channel', data.channel || 0)
            .timestamp(new Date(timestamp));

        if (data.amplitude_data && Array.isArray(data.amplitude_data)) {
            data.amplitude_data.forEach((value, index) => {
                if (typeof value === 'number') {
                    point.floatField(`amplitude_${index}`, value);
                }
            });
        }

        this.writeAPI.writePoint(point);
    }

    start() {
        this.server.listen(CONFIG.PORT, () => {
            logger.info(`CSI Backend Server ${VERSION} running on port ${CONFIG.PORT}`);
            logger.info(`WebSocket server running on port ${CONFIG.WS_PORT}`);
            logger.info('Architecture: HTTP + WebSocket (MQTT-free)');
        });

        // Graceful shutdown
        process.on('SIGTERM', () => {
            logger.info('SIGTERM received, shutting down gracefully');
            this.server.close(() => {
                if (this.redisClient) this.redisClient.quit();
                process.exit(0);
            });
        });
    }
}

// Start the server
if (require.main === module) {
    const server = new CSIServerV2();
    server.start();
}

module.exports = CSIServerV2;