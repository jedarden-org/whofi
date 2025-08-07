/**
 * Main Application Component
 * CSI Positioning System Frontend
 */

import React, { useState, useEffect } from 'react';
import { Routes, Route } from 'react-router-dom';
import {
  Box,
  AppBar,
  Toolbar,
  Typography,
  Drawer,
  List,
  ListItem,
  ListItemButton,
  ListItemIcon,
  ListItemText,
  IconButton,
  Alert,
  Snackbar
} from '@mui/material';
import {
  Menu as MenuIcon,
  Dashboard as DashboardIcon,
  LocationOn as LocationIcon,
  GridOn as HeatmapIcon,
  Timeline as SignalIcon,
  Tune as CalibrateIcon,
  Sensors as SensorsIcon,
  Settings as SettingsIcon,
  Analytics as AnalyticsIcon,
  WifiFind as WifiIcon
} from '@mui/icons-material';

// Import components
import Dashboard from './components/Dashboard';
import PositionView from './components/PositionView';
import HeatmapView from './components/HeatmapView';
import SignalAnalyzer from './components/SignalAnalyzer';
import CalibrationInterface from './components/CalibrationInterface';
import NodesView from './components/NodesView';
import Analytics from './components/Analytics';
import Settings from './components/Settings';
import { WebSocketProvider } from './context/WebSocketContext';
import { useApi } from './hooks/useApi';

const DRAWER_WIDTH = 240;

function App() {
  const [mobileOpen, setMobileOpen] = useState(false);
  const [connectionStatus, setConnectionStatus] = useState('connecting');
  const [notification, setNotification] = useState(null);

  const { data: healthData, isError: healthError } = useApi('/api/health', {
    refetchInterval: 30000,
  });

  useEffect(() => {
    if (healthData) {
      setConnectionStatus('connected');
    } else if (healthError) {
      setConnectionStatus('error');
    }
  }, [healthData, healthError]);

  const handleDrawerToggle = () => {
    setMobileOpen(!mobileOpen);
  };

  const handleNotificationClose = () => {
    setNotification(null);
  };

  const menuItems = [
    { text: 'Dashboard', icon: <DashboardIcon />, path: '/' },
    { text: 'Position Tracking', icon: <LocationIcon />, path: '/position' },
    { text: 'Heatmap View', icon: <HeatmapIcon />, path: '/heatmap' },
    { text: 'Signal Analyzer', icon: <SignalIcon />, path: '/signals' },
    { text: 'Calibration', icon: <CalibrateIcon />, path: '/calibration' },
    { text: 'Node Status', icon: <SensorsIcon />, path: '/nodes' },
    { text: 'Analytics', icon: <AnalyticsIcon />, path: '/analytics' },
    { text: 'Settings', icon: <SettingsIcon />, path: '/settings' }
  ];

  const drawer = (
    <Box>
      <Box
        sx={{
          display: 'flex',
          alignItems: 'center',
          padding: 2,
          backgroundColor: 'primary.main',
          color: 'primary.contrastText'
        }}
      >
        <WifiIcon sx={{ marginRight: 1 }} />
        <Typography variant="h6" noWrap>
          CSI Positioning
        </Typography>
      </Box>
      
      <List>
        {menuItems.map((item) => (
          <ListItem key={item.text} disablePadding>
            <ListItemButton
              component="a"
              href={item.path}
              onClick={(e) => {
                e.preventDefault();
                window.history.pushState({}, '', item.path);
                setMobileOpen(false);
              }}
            >
              <ListItemIcon>{item.icon}</ListItemIcon>
              <ListItemText primary={item.text} />
            </ListItemButton>
          </ListItem>
        ))}
      </List>
      
      <Box sx={{ position: 'absolute', bottom: 16, left: 16, right: 16 }}>
        <Typography variant="caption" color="text.secondary">
          Status: {connectionStatus === 'connected' ? '🟢 Online' : 
                   connectionStatus === 'error' ? '🔴 Offline' : '🟡 Connecting'}
        </Typography>
        {healthData && (
          <Typography variant="caption" display="block" color="text.secondary">
            Nodes: {healthData.nodes} | Uptime: {Math.floor(healthData.uptime / 1000)}s
          </Typography>
        )}
      </Box>
    </Box>
  );

  return (
    <WebSocketProvider onNotification={setNotification}>
      <Box sx={{ display: 'flex' }}>
        {/* App Bar */}
        <AppBar
          position="fixed"
          sx={{
            width: { sm: `calc(100% - ${DRAWER_WIDTH}px)` },
            ml: { sm: `${DRAWER_WIDTH}px` },
          }}
        >
          <Toolbar>
            <IconButton
              color="inherit"
              aria-label="open drawer"
              edge="start"
              onClick={handleDrawerToggle}
              sx={{ mr: 2, display: { sm: 'none' } }}
            >
              <MenuIcon />
            </IconButton>
            <Typography variant="h6" noWrap component="div" sx={{ flexGrow: 1 }}>
              WiFi CSI Indoor Positioning System
            </Typography>
            <Box sx={{ display: 'flex', alignItems: 'center' }}>
              <Box
                sx={{
                  width: 10,
                  height: 10,
                  borderRadius: '50%',
                  backgroundColor: connectionStatus === 'connected' ? 'success.main' :
                                 connectionStatus === 'error' ? 'error.main' : 'warning.main',
                  marginRight: 1
                }}
              />
              <Typography variant="body2">
                {connectionStatus === 'connected' ? 'Connected' :
                 connectionStatus === 'error' ? 'Disconnected' : 'Connecting'}
              </Typography>
            </Box>
          </Toolbar>
        </AppBar>

        {/* Drawer */}
        <Box
          component="nav"
          sx={{ width: { sm: DRAWER_WIDTH }, flexShrink: { sm: 0 } }}
        >
          <Drawer
            variant="temporary"
            open={mobileOpen}
            onClose={handleDrawerToggle}
            ModalProps={{ keepMounted: true }}
            sx={{
              display: { xs: 'block', sm: 'none' },
              '& .MuiDrawer-paper': { boxSizing: 'border-box', width: DRAWER_WIDTH },
            }}
          >
            {drawer}
          </Drawer>
          <Drawer
            variant="permanent"
            sx={{
              display: { xs: 'none', sm: 'block' },
              '& .MuiDrawer-paper': { boxSizing: 'border-box', width: DRAWER_WIDTH },
            }}
            open
          >
            {drawer}
          </Drawer>
        </Box>

        {/* Main Content */}
        <Box
          component="main"
          sx={{
            flexGrow: 1,
            p: 3,
            width: { sm: `calc(100% - ${DRAWER_WIDTH}px)` },
            marginTop: '64px'
          }}
        >
          <Routes>
            <Route path="/" element={<Dashboard />} />
            <Route path="/position" element={<PositionView />} />
            <Route path="/heatmap" element={<HeatmapView />} />
            <Route path="/signals" element={<SignalAnalyzer />} />
            <Route path="/calibration" element={<CalibrationInterface />} />
            <Route path="/nodes" element={<NodesView />} />
            <Route path="/analytics" element={<Analytics />} />
            <Route path="/settings" element={<Settings />} />
          </Routes>
        </Box>

        {/* Notifications */}
        <Snackbar
          open={!!notification}
          autoHideDuration={6000}
          onClose={handleNotificationClose}
          anchorOrigin={{ vertical: 'bottom', horizontal: 'right' }}
        >
          {notification && (
            <Alert
              onClose={handleNotificationClose}
              severity={notification.type || 'info'}
              sx={{ width: '100%' }}
            >
              {notification.message}
            </Alert>
          )}
        </Snackbar>
      </Box>
    </WebSocketProvider>
  );
}

export default App;