#!/usr/bin/env node

/**
 * ESP32 Telemetry Simulation Demo
 * Demonstrates realistic WiFi CSI data generation and multi-transmitter simulation
 * Run with: node demo-telemetry.js
 */

const { ESP32CSISimulator } = require('./esp32-telemetry-simulation.test.js');

console.log('🏠 ESP32 Multi-Transmitter Telemetry Simulation Demo');
console.log('=' .repeat(60));

// Create simulated ESP32 devices in different locations
const devices = [
  new ESP32CSISimulator('esp32-living-room', { x: 0, y: 0, z: 1.2 }),
  new ESP32CSISimulator('esp32-kitchen', { x: 5, y: 3, z: 1.2 }),
  new ESP32CSISimulator('esp32-bedroom', { x: -3, y: 4, z: 1.2 }),
  new ESP32CSISimulator('esp32-office', { x: 2, y: -2, z: 1.2 }),
  new ESP32CSISimulator('esp32-bathroom', { x: -1, y: 2, z: 1.2 })
];

console.log(`\n📡 Created ${devices.length} ESP32 simulators:`);
devices.forEach((device, index) => {
  console.log(`  ${index + 1}. ${device.deviceId} @ (${device.location.x}, ${device.location.y}, ${device.location.z}m)`);
  console.log(`     MAC: ${device.macAddress}, Channel: ${device.channel}`);
});

console.log('\n🧪 Generating Sample CSI Data:');
console.log('-'.repeat(40));

// Generate and display sample CSI data from each device
devices.forEach((device, index) => {
  console.log(`\n📊 ${device.deviceId} CSI Data Sample:`);
  
  const csiData = device.generateCSIData();
  const heartbeat = device.generateHeartbeat();
  const stats = device.generateStats();
  
  console.log(`   Timestamp: ${new Date(csiData.timestamp).toISOString()}`);
  console.log(`   Sequence: ${csiData.sequence_number}`);
  console.log(`   WiFi RSSI: ${csiData.wifi_info.rssi} dBm`);
  console.log(`   Channel: ${csiData.wifi_info.channel} (${csiData.wifi_info.bandwidth})`);
  console.log(`   CSI Matrix: ${csiData.csi_data.subcarrier_count} subcarriers`);
  console.log(`   Temperature: ${csiData.environmental.temperature}°C`);
  console.log(`   Free Heap: ${(heartbeat.system_info.free_heap / 1024).toFixed(1)} KB`);
  console.log(`   Positioning Accuracy: ${stats.positioning_accuracy}m`);
  
  // Show first few subcarriers for example
  console.log(`   Sample Subcarriers:`);
  csiData.csi_data.matrix.slice(0, 3).forEach(sc => {
    console.log(`     SC${sc.subcarrier}: amp=${sc.amplitude.toFixed(1)}, phase=${sc.phase.toFixed(2)}`);
  });
});

console.log('\n⚡ Simulating Concurrent Data Transmission:');
console.log('-'.repeat(50));

// Simulate concurrent data transmission
const concurrentData = devices.map(device => {
  const data = device.generateCSIData();
  return {
    device_id: device.deviceId,
    timestamp: data.timestamp,
    payload_size: JSON.stringify(data).length,
    rssi: data.wifi_info.rssi,
    subcarriers: data.csi_data.subcarrier_count,
    location: data.location
  };
});

console.log('📦 Concurrent transmission summary:');
concurrentData.forEach(data => {
  console.log(`   ${data.device_id}: ${(data.payload_size / 1024).toFixed(1)} KB, RSSI ${data.rssi} dBm`);
});

const totalPayload = concurrentData.reduce((sum, data) => sum + data.payload_size, 0);
console.log(`\n📈 Total concurrent payload: ${(totalPayload / 1024).toFixed(1)} KB from ${devices.length} devices`);

console.log('\n🔄 Simulating Rapid Sequential Transmission:');
console.log('-'.repeat(45));

const device = devices[0]; // Use living room device
console.log(`Using ${device.deviceId} for rapid transmission demo...`);

for (let i = 0; i < 5; i++) {
  const data = device.generateCSIData();
  console.log(`   Seq ${data.sequence_number}: ${new Date(data.timestamp).toLocaleTimeString()}, RSSI ${data.wifi_info.rssi} dBm`);
}

console.log('\n🎯 API Endpoints for ESP32 Integration:');
console.log('-'.repeat(42));
console.log('   CSI Data:     POST /api/csi/{nodeId}/data');
console.log('   Heartbeat:    POST /api/csi/{nodeId}/heartbeat');
console.log('   Statistics:   POST /api/csi/{nodeId}/stats');
console.log('   Bulk Data:    POST /api/csi/bulk');
console.log('   WebSocket:    ws://localhost/ws');

console.log('\n🧪 Integration Test Features:');
console.log('-'.repeat(30));
console.log('   ✅ Single ESP32 telemetry validation');
console.log('   ✅ Multi-transmitter concurrent testing');
console.log('   ✅ Bulk data processing');
console.log('   ✅ WebSocket real-time streaming');
console.log('   ✅ Load testing & performance');
console.log('   ✅ CSI data quality validation');

console.log('\n🚀 Ready for Production Testing!');
console.log('   Run: npm run test:telemetry');
console.log('=' .repeat(60));