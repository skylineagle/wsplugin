const WebSocket = require('ws');
const { createCanvas } = require('canvas');

const PORT = 8080;
const wss = new WebSocket.Server({ port: PORT });

console.log(`Mock Video Server started on ws://localhost:${PORT}`);

// Animation variables
let posX = 0;
const width = 640;
const height = 480;
const canvas = createCanvas(width, height);
const ctx = canvas.getContext('2d');

wss.on('connection', (ws) => {
    console.log('Client connected. Sending "video" stream...');

    const streamInterval = setInterval(() => {
        // 1. Clear Background
        ctx.fillStyle = '#1a1a1a';
        ctx.fillRect(0, 0, width, height);

        // 2. Draw a moving "object" (Red Circle)
        ctx.beginPath();
        ctx.arc(posX, height / 2, 50, 0, Math.PI * 2);
        ctx.fillStyle = '#ff4444';
        ctx.fill();

        // 3. Add dynamic text (Timestamp/Frame)
        ctx.fillStyle = 'white';
        ctx.font = '30px Arial';
        ctx.fillText(`Live Stream: ${new Date().toLocaleTimeString()}`, 20, 50);
        ctx.fillText(`Frame X: ${Math.floor(posX)}`, 20, 90);

        // Update position for next frame
        posX = (posX + 10) % width;

        // 4. Convert Canvas to JPEG Buffer
        const buffer = canvas.toBuffer('image/jpeg', { quality: 0.7 });

        // 5. Send over WebSocket
        if (ws.readyState === WebSocket.OPEN) {
            ws.send(buffer);
        }
    }, 100); // ~10 Frames Per Second

    ws.on('close', () => {
        console.log('Client disconnected');
        clearInterval(streamInterval);
    });

    ws.on('error', (err) => {
        console.error('WS Error:', err);
        clearInterval(streamInterval);
    });
});