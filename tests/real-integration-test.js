/**
 * REAL Integration Tests - No Mocking, Full Backend Validation
 * Tests actual data processing, storage, and retrieval capabilities
 * 
 * Requirements:
 * 1. Backend must actually process and store CSI data
 * 2. WebSocket must stream real processed data
 * 3. Multi-transmitter data must be differentiated and stored
 * 4. API must return actual processed results, not just HTTP 200
 */

const axios = require('axios');
const WebSocket = require('ws');
const { exec } = require('child_process');
const { promisify } = require('util');

const execAsync = promisify(exec);

// Import our ESP32 simulator
const { ESP32CSISimulator } = require('./esp32-simulator.js');

describe('REAL Integration Tests - Full Backend Validation', () => {
  const CONTAINER_NAME = 'whofi-real-test';
  const BASE_URL = 'http://localhost:80';
  const WS_URL = 'ws://localhost:80/ws';
  
  // Create test devices
  const testDevices = [
    new ESP32CSISimulator('esp32-test-01', { x: 0, y: 0, z: 1.2 }),
    new ESP32CSISimulator('esp32-test-02', { x: 5, y: 3, z: 1.2 }),
    new ESP32CSISimulator('esp32-test-03', { x: -2, y: 4, z: 1.2 })
  ];

  beforeAll(async () => {
    console.log('🔥 Starting REAL integration test with full backend...');
    
    // Clean up existing containers
    try {
      await execAsync(`docker stop ${CONTAINER_NAME} || true`);
      await execAsync(`docker rm ${CONTAINER_NAME} || true`);
    } catch (error) {
      // Ignore cleanup errors
    }
    
    // Start unified container with ALL backend services
    // TODO: Need to include MQTT, InfluxDB, Redis for real testing
    console.log('🚀 Starting unified container with full backend stack...');
    const runCommand = `docker run -d --name ${CONTAINER_NAME} -p 80:80 -e NODE_ENV=production whofi-unified:test`;
    await execAsync(runCommand);
    
    // Wait longer for full backend initialization
    console.log('⏳ Waiting for full backend stack to initialize...');
    await new Promise(resolve => setTimeout(resolve, 30000));
  }, 120000);

  afterAll(async () => {
    try {
      await execAsync(`docker stop ${CONTAINER_NAME} || true`);
      await execAsync(`docker rm ${CONTAINER_NAME} || true`);
    } catch (error) {
      console.warn('Cleanup warning:', error.message);
    }
  });

  describe('REAL Test 1: Actual Data Processing Validation', () => {
    test('should actually process and confirm CSI data reception', async () => {
      const device = testDevices[0];
      const csiData = device.generateCSIData();
      
      // Submit CSI data
      const response = await axios.post(`${BASE_URL}/api/csi/${device.deviceId}/data`, csiData, {
        headers: { 'Content-Type': 'application/json' },
        timeout: 10000
      });
      
      // REAL validation - must be 200/201, no error acceptance
      expect(response.status).toBe(200);
      expect(response.data).toHaveProperty('success', true);
      expect(response.data).toHaveProperty('nodeId', device.deviceId);
      
      // Validate backend actually processed the data
      expect(response.data).toHaveProperty('timestamp');
      expect(response.data.timestamp).toBeGreaterThan(0);
      
      console.log('✅ REAL data processing confirmed:', response.data);
    });

    test('should store and retrieve processed CSI data', async () => {
      const device = testDevices[0];
      const csiData = device.generateCSIData();
      
      // Submit data
      await axios.post(`${BASE_URL}/api/csi/${device.deviceId}/data`, csiData, {
        headers: { 'Content-Type': 'application/json' },
        timeout: 10000
      });
      
      // Wait for processing
      await new Promise(resolve => setTimeout(resolve, 2000));
      
      // Retrieve stored data - this must work for real validation
      const retrieveResponse = await axios.get(`${BASE_URL}/api/csi/${device.deviceId}/latest`, {
        timeout: 10000
      });
      
      expect(retrieveResponse.status).toBe(200);
      expect(retrieveResponse.data).toHaveProperty('device_id', device.deviceId);
      expect(retrieveResponse.data).toHaveProperty('csi_data');
      expect(retrieveResponse.data.csi_data).toHaveProperty('subcarrier_count', 52);
      
      console.log('✅ REAL data storage/retrieval confirmed');
    });
  });

  describe('REAL Test 2: Multi-Transmitter Data Differentiation', () => {
    test('should process concurrent data from multiple ESP32s and differentiate them', async () => {
      console.log('🔄 REAL concurrent testing with data differentiation...');
      
      // Submit data from all test devices concurrently
      const promises = testDevices.map(async (device) => {
        const csiData = device.generateCSIData();
        return axios.post(`${BASE_URL}/api/csi/${device.deviceId}/data`, csiData, {
          headers: { 'Content-Type': 'application/json' },
          timeout: 10000
        });
      });

      const responses = await Promise.all(promises);
      
      // ALL must succeed (no error tolerance)
      responses.forEach((response, index) => {
        expect(response.status).toBe(200);
        expect(response.data.success).toBe(true);
        expect(response.data.nodeId).toBe(testDevices[index].deviceId);
      });
      
      // Validate backend differentiates between devices
      await new Promise(resolve => setTimeout(resolve, 3000));
      
      for (const device of testDevices) {
        const retrieveResponse = await axios.get(`${BASE_URL}/api/csi/${device.deviceId}/latest`);
        expect(retrieveResponse.status).toBe(200);
        expect(retrieveResponse.data.device_id).toBe(device.deviceId);
      }
      
      console.log('✅ REAL multi-transmitter differentiation confirmed');
    });
  });

  describe('REAL Test 3: WebSocket Data Streaming Validation', () => {
    test('should stream actual processed CSI data via WebSocket', (done) => {
      const ws = new WebSocket(WS_URL);
      let realDataReceived = false;
      
      const timeout = setTimeout(() => {
        ws.close();
        if (!realDataReceived) {
          done(new Error('WebSocket did not receive actual CSI data'));
        }
      }, 15000);
      
      ws.on('open', async () => {
        console.log('🔗 WebSocket connected, submitting real data...');
        
        // Submit actual CSI data
        const device = testDevices[1];
        const csiData = device.generateCSIData();
        
        await axios.post(`${BASE_URL}/api/csi/${device.deviceId}/data`, csiData, {
          headers: { 'Content-Type': 'application/json' },
          timeout: 10000
        });
      });
      
      ws.on('message', (data) => {
        try {
          const message = JSON.parse(data.toString());
          
          // REAL validation - must contain actual CSI data
          if (message.type === 'csi_data' || message.csi_data) {
            expect(message).toHaveProperty('device_id');
            expect(message).toHaveProperty('timestamp');
            if (message.csi_data) {
              expect(message.csi_data).toHaveProperty('subcarrier_count');
            }
            
            realDataReceived = true;
            clearTimeout(timeout);
            ws.close();
            console.log('✅ REAL WebSocket CSI data streaming confirmed');
            done();
          }
        } catch (parseError) {
          // Only accept actual JSON CSI data, not generic messages
          console.log('⚠️ Non-CSI WebSocket message:', data.toString().substring(0, 100));
        }
      });
      
      ws.on('error', (error) => {
        clearTimeout(timeout);
        done(new Error(`WebSocket failed: ${error.message}`));
      });
    });
  });

  describe('REAL Test 4: Backend Service Integration', () => {
    test('should validate backend services are actually running and processing', async () => {
      // Check backend health endpoint - must return actual service status
      const healthResponse = await axios.get(`${BASE_URL}/api/health`);
      expect(healthResponse.status).toBe(200);
      
      // Validate backend services are connected and operational
      expect(healthResponse.data).toHaveProperty('status', 'healthy');
      expect(healthResponse.data).toHaveProperty('services');
      
      // Check critical services are operational (not just started)
      const services = healthResponse.data.services;
      expect(services.database).toBe('connected');  // InfluxDB or similar
      expect(services.cache).toBe('connected');     // Redis
      expect(services.mqtt).toBe('connected');      // MQTT broker
      
      console.log('✅ REAL backend service integration confirmed');
    });
  });

  describe('REAL Test 5: Data Processing Performance', () => {
    test('should process high-frequency ESP32 data under load', async () => {
      console.log('⚡ REAL load testing with actual data processing...');
      
      const requestCount = 50;
      const device = testDevices[2];
      const startTime = Date.now();
      
      const promises = Array.from({length: requestCount}, async (_, i) => {
        const csiData = device.generateCSIData();
        const response = await axios.post(`${BASE_URL}/api/csi/${device.deviceId}/data`, csiData, {
          headers: { 'Content-Type': 'application/json' },
          timeout: 5000
        });
        return { index: i, status: response.status, success: response.data.success };
      });
      
      const results = await Promise.all(promises);
      const endTime = Date.now();
      
      // ALL requests must succeed for real validation
      const successful = results.filter(r => r.status === 200 && r.success === true);
      expect(successful.length).toBe(requestCount);
      
      const throughput = requestCount / ((endTime - startTime) / 1000);
      console.log(`✅ REAL processing: ${requestCount} requests, ${throughput.toFixed(1)} req/s`);
      
      // Validate actual data processing speed
      expect(throughput).toBeGreaterThan(5); // At least 5 req/s actual processing
    }, 30000);
  });
});

module.exports = {
  testEnvironment: 'node',
  testTimeout: 120000
};