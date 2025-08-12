/**
 * ESP32 CSI Data Simulator Utility
 * Generates realistic WiFi Channel State Information data for testing
 */

// ESP32 CSI Data Generator - Realistic WiFi Channel State Information
class ESP32CSISimulator {
  constructor(deviceId, location = { x: 0, y: 0, z: 0 }) {
    this.deviceId = deviceId;
    this.location = location;
    this.macAddress = this.generateRandomMAC();
    this.channel = Math.floor(Math.random() * 13) + 1; // WiFi channels 1-13
    this.sequenceNumber = 0;
  }

  generateRandomMAC() {
    // ESP32 MAC address format (24:6F:28:XX:XX:XX for ESP32 devices)
    const prefix = '24:6F:28';
    const suffix = Array.from({length: 3}, () => 
      Math.floor(Math.random() * 256).toString(16).padStart(2, '0').toUpperCase()
    ).join(':');
    return `${prefix}:${suffix}`;
  }

  // Generate realistic CSI data based on ESP32-S3 capabilities
  generateCSIData() {
    this.sequenceNumber++;
    
    // ESP32-S3 supports 52 subcarriers in 20MHz mode (OFDM)
    const subcarriers = 52;
    const timestamp = Date.now();
    
    // Realistic RSSI range for indoor positioning (-30 to -90 dBm)
    const rssi = -30 - (Math.random() * 60); // -30 to -90 dBm
    
    // Channel State Information matrix (complex numbers for amplitude/phase)
    const csiMatrix = [];
    for (let i = 0; i < subcarriers; i++) {
      // Each subcarrier has amplitude and phase
      const amplitude = Math.random() * 100 + 10; // 10-110 range
      const phase = Math.random() * 2 * Math.PI - Math.PI; // -π to π
      csiMatrix.push({
        subcarrier: i,
        amplitude: parseFloat(amplitude.toFixed(2)),
        phase: parseFloat(phase.toFixed(4)),
        real: parseFloat((amplitude * Math.cos(phase)).toFixed(2)),
        imaginary: parseFloat((amplitude * Math.sin(phase)).toFixed(2))
      });
    }

    return {
      device_id: this.deviceId,
      timestamp: timestamp,
      sequence_number: this.sequenceNumber,
      mac_address: this.macAddress,
      location: this.location,
      wifi_info: {
        channel: this.channel,
        rssi: parseFloat(rssi.toFixed(1)),
        noise_floor: parseFloat((rssi - Math.random() * 10 - 5).toFixed(1)), // 5-15 dB below RSSI
        bandwidth: '20MHz',
        mode: 'HT20'
      },
      csi_data: {
        subcarrier_count: subcarriers,
        sampling_rate: 1000, // 1 kHz
        matrix: csiMatrix
      },
      environmental: {
        temperature: parseFloat((20 + Math.random() * 10).toFixed(1)), // 20-30°C
        free_heap: Math.floor(200000 + Math.random() * 100000), // 200-300KB
        uptime: Math.floor(timestamp / 1000), // seconds since boot
        wifi_strength: parseFloat(rssi.toFixed(1))
      }
    };
  }

  // Generate realistic heartbeat data
  generateHeartbeat() {
    return {
      device_id: this.deviceId,
      timestamp: Date.now(),
      status: 'online',
      firmware_version: '1.1.7',
      wifi_connected: true,
      mac_address: this.macAddress,
      location: this.location,
      system_info: {
        free_heap: Math.floor(200000 + Math.random() * 100000),
        uptime: Math.floor(Date.now() / 1000),
        cpu_temp: parseFloat((35 + Math.random() * 15).toFixed(1)), // 35-50°C
        wifi_rssi: -30 - (Math.random() * 60)
      }
    };
  }

  // Generate node statistics
  generateStats() {
    return {
      device_id: this.deviceId,
      timestamp: Date.now(),
      packets_sent: Math.floor(Math.random() * 10000),
      packets_received: Math.floor(Math.random() * 10000),
      data_rate: parseFloat((Math.random() * 100).toFixed(2)), // MB/s
      error_rate: parseFloat((Math.random() * 0.05).toFixed(4)), // 0-5% error
      signal_quality: Math.floor(Math.random() * 100), // 0-100%
      positioning_accuracy: parseFloat((0.5 + Math.random() * 2).toFixed(2)) // 0.5-2.5m accuracy
    };
  }
}

module.exports = {
  ESP32CSISimulator
};