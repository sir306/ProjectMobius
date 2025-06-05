/*
MIT License

Copyright (c) 2025 ProjectMobius contributors
Nicholas R. Harding and Peter Thompson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

// server.js
const fs = require('fs');
const path = require('path');
const WebSocket = require('ws');
const { randomUUID } = require('crypto');

// 1) Load port from config.json or fallback to env/8080
let port;
try {
  const cfg = JSON.parse(fs.readFileSync(path.resolve(__dirname, 'config.json'), 'utf8'));
  port = cfg.port;
  console.log(`Loaded port ${port} from config.json`);
} catch {
  console.warn('Could not read config.json; falling back to env or default');
  port = process.env.PORT || 8080;
}

// registry now tracks unreal socket, qt socket, and currentTime
//        maps clientId → { unreal: WebSocket, qt: WebSocket|null, currentTime: any }
const registry = {};
// Performance monitoring
let messageCount = 0;
let lastStatsTime = Date.now();

const wss = new WebSocket.Server({
  port,
  // Optimize WebSocket server settings
  perMessageDeflate: false, // Disable compression for speed
}, () => {
  //console.log(`WebSocket server listening on ws://localhost:${port}`);
});

wss.on('connection', ws => {
  //console.log(`↔ New raw connection`);

  ws.once('message', raw => {
    let msg;
    try {
      msg = JSON.parse(raw);
    } catch {
      ws.send(JSON.stringify({ error: 'first message must be JSON register' }));
      return ws.close();
    }

    if (msg.type !== 'register' || (msg.role !== 'unreal' && msg.role !== 'qt')) {
      ws.send(JSON.stringify({ error: 'invalid register message' }));
      return ws.close();
    }

    // Unreal registers → create a slot with currentTime initialized to null
    if (msg.role === 'unreal') {
      const id = randomUUID();
      ws.clientId = id;
      registry[id] = {
        unreal: ws,
        qt: null,
        currentTime: 0.0, // Initialize currentTime to 0
        currentAgentCount: 0, // Initialize agent count
        lastUpdateTime: 0,
        messageQueue: [],
        isProcessing: false,
        stats: { sent: 0, dropped: 0 }
      };
      ws.send(JSON.stringify({ type: 'assignId', id }));
      //console.log(`🎮 Unreal registered → assigned ID=${id}`);

      // Qt registers → attach to existing slot
    } else {
      const id = msg.id;
      if (!id || !registry[id]) {
        ws.send(JSON.stringify({ error: 'unknown id, cannot register qt' }));
        //console.warn(`❌ Qt tried to register with bad id=${id}`);
        return ws.close();
      }
      ws.clientId = id;
      registry[id].qt = ws;

      // Optimize Qt socket
      if (ws._socket) {
        ws._socket.setNoDelay(true);
        ws._socket.setKeepAlive(true, 1000);
      }

      ws.send(JSON.stringify({ status: 'qt registered' }));
      //console.log(`📊 Qt registered to ID=${id}`);
    }

    // After registration, handle messages from both Unreal & Qt
    ws.on('message', raw2 => {
      let payload;
      try {
        payload = JSON.parse(raw2);
      } catch {
        return; // Ignore non-JSON messages for now
      }



      const entry = registry[ws.clientId];
      if (!entry) return;

      // ———————————————————————————
      // A) If Qt asks for “getData”, reply immediately & return
      // ———————————————————————————
      if (entry.qt === ws && payload.action === 'getData') {
        // ws.send(JSON.stringify({
        //   action: 'updateCurrentTime',
        //   currentTime: entry.currentTime,
        //   timestamp: Date.now()
        // }));
        // ws.send(JSON.stringify({
        //   action: 'updateAgentCount',
        //   agentCount: entry.currentAgentCount,
        //   timestamp: Date.now()
        // }));
        ws.send(JSON.stringify({
          action: 'updateLiveData',
          time: entry.currentTime,
          count: entry.currentAgentCount,
          timestamp: Date.now()
        }));
        return;
      }

      // ———————————————————————————
      // B) If Unreal pushes updates to registry
      // ———————————————————————————
      if (ws === entry.unreal) {
        switch (payload.action) {
          case 'timeUpdate':
            entry.currentTime = payload.time;
            break;
          case 'agentCountUpdate':
            entry.currentAgentCount = payload.count;
            break;
          case 'updateLiveData':
            entry.currentTime = payload.currentTime;
            entry.currentAgentCount = payload.count;
            break;
        }
      }


      //  —————————————————————————————————————————————
      // C) Otherwise, route unreal ↔ qt as before
      //  —————————————————————————————————————————————
      const peer = (ws === entry.unreal) ? entry.qt : entry.unreal;
      if (peer && peer.readyState === WebSocket.OPEN) {
        peer.send(raw2);
      }
    });

    // 4) Cleanup on close (same as before)…
    ws.on('close', () => {
      const id = ws.clientId;
      const entry = registry[id];
      //console.log(`✖ Connection closed for ID=${id}`);
      if (!entry) return;

      if (ws === entry.unreal) {
        delete registry[id];
        //console.log(`Removed slot for Unreal ID=${id}`);
      } else if (ws === entry.qt) {
        entry.qt = null;
        //console.log(`Qt unplugged from ID=${id}`);
      }

      const unrealLeft = Object.keys(registry).length;
      //console.log(`Unreal instances remaining: ${unrealLeft}`);
      if (unrealLeft === 0) {
        //console.log(`No Unreal clients left, shutting server down…`);
        wss.clients.forEach(c => {
          if (c.readyState === WebSocket.OPEN) {
            c.send(JSON.stringify({ status: 'server shutting down' }));
            c.close(1000, 'shutdown');
          }
        });
        wss.close(() => process.exit(0));
      }
    });
  });
});


function handleTimeUpdateWithLatest(entry, payload) {
  entry.currentTime = payload.time;
  entry.pendingUpdate = true;

  // Only process if not already processing
  if (!entry.isProcessing) {
    entry.isProcessing = true;
    // Use setImmediate for next tick processing
    setImmediate(() => {
      if (entry.pendingUpdate) {
        sendToQt(entry, entry.currentTime);
        entry.pendingUpdate = false;
      }
      entry.isProcessing = false;
    });
  }
}

function sendToQt(entry, currentTime) {
  if (entry.qt && entry.qt.readyState === WebSocket.OPEN) {
    const message = JSON.stringify({
      action: 'updateCurrentTime',
      currentTime: currentTime,
      timestamp: Date.now() // Add server timestamp for latency measurement
    });

    try {
      entry.qt.send(message);
      entry.stats.sent++;
    } catch (error) {
      //console.error(`Failed to send to Qt: ${error.message}`);
    }
  }
}

function sendToQtBinary(entry, currentTime) {
  const wsQt = entry.qt;
  if (!wsQt || wsQt.readyState !== WebSocket.OPEN) return;
  const buf = new ArrayBuffer(16);
  const dv = new DataView(buf);
  dv.setFloat64(0, currentTime, true);
  dv.setFloat64(8, Date.now(), true);
  wsQt.send(buf);  // send as binary
  entry.stats.sent++;
}