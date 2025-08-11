#!/bin/bash

echo "🚀 Starting Unified CSI Container..."

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

# Test backend health
echo "🔍 Testing backend health..."
for i in {1..10}; do
    if curl -f http://127.0.0.1:3001/api/health 2>/dev/null; then
        echo "✅ Backend health check passed"
        break
    fi
    echo "⏳ Backend health check attempt $i/10..."
    sleep 2
done

# Start nginx to serve frontend and proxy API requests
echo "🌐 Starting nginx reverse proxy on port 80..."
nginx -g "daemon off;"