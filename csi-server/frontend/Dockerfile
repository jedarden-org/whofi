# Multi-stage build
# Use public ECR gallery to avoid Docker Hub rate limits
FROM public.ecr.aws/docker/library/node:18-alpine AS builder

WORKDIR /app

# Copy package files from frontend directory
COPY frontend/package*.json ./

# Install dependencies
RUN npm ci

# Copy all remaining frontend files
COPY frontend/ .

# Build the application
RUN npm run build

# Production stage
# Use public ECR gallery to avoid Docker Hub rate limits
FROM public.ecr.aws/nginx/nginx:alpine

# Copy built application
COPY --from=builder /app/build /usr/share/nginx/html

# Copy nginx configuration will be mounted as volume

# Expose port 80
EXPOSE 80

# Health check
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
  CMD wget --no-verbose --tries=1 --spider http://localhost/health || exit 1

# Start nginx
CMD ["nginx", "-g", "daemon off;"]