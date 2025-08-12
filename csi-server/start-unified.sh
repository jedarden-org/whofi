#!/bin/bash

echo "🚀 Starting Unified CSI Container v1.2.17 - NGINX PROXY FIX ACTIVE..."

# Create required directories for nginx
mkdir -p /var/log/nginx /var/lib/nginx/logs /var/cache/nginx

# Start backend Node.js server in background
echo "📡 Starting backend server on port 3001..."
cd /app && PORT=3001 NODE_ENV=test node server.js > /tmp/backend.log 2>&1 &
BACKEND_PID=$!

# Show backend startup logs
echo "📋 Backend startup logs:"
sleep 2
if [ -f /tmp/backend.log ]; then
    cat /tmp/backend.log
fi

# Wait for backend to initialize
echo "⏳ Waiting for backend initialization..."
sleep 8

# Check if backend is running
if ! kill -0 $BACKEND_PID 2>/dev/null; then
    echo "❌ Backend failed to start"
    exit 1
fi

echo "✅ Backend started with PID: $BACKEND_PID"

# Start nginx reverse proxy and test unified health endpoint
echo "🌐 Starting nginx reverse proxy on port 80..."
nginx -g "daemon on;" &
NGINX_PID=$!

# Wait for nginx to initialize
sleep 3

# Test unified health endpoint (ESP32s will use this path)
echo "🔍 Testing unified health endpoint via nginx proxy..."
for i in {1..10}; do
    if curl -f http://127.0.0.1:80/api/health 2>/dev/null; then
        echo "✅ Unified health check passed"
        break
    fi
    echo "⏳ Unified health check attempt $i/10..."
    sleep 2
done

# Switch nginx to foreground mode for container lifecycle
echo "🔄 Switching nginx to foreground mode..."
nginx -s quit  # Stop background nginx
sleep 1
nginx -g "daemon off;"  # Start in foreground