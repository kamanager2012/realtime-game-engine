// k6 load test for Poker Engine WebSocket server.
// Usage:
//   k6 run tests/load/k6-test.js --env BASE_URL=ws://localhost:9001
//
// Tests: HTTP health, table creation via matchmaking, concurrent WS connections.

import ws from 'k6/ws';
import http from 'k6/http';
import { check, sleep } from 'k6';

const BASE_URL = __ENV.BASE_URL || 'http://localhost:9001';
const WS_URL = BASE_URL.replace('http', 'ws');
const VUS = __ENV.VUS ? parseInt(__ENV.VUS) : 50;
const DURATION = __ENV.DURATION || '30s';

export const options = {
  vus: VUS,
  duration: DURATION,
  thresholds: {
    http_req_duration: ['p(95)<200'],       // 95% of HTTP requests < 200ms
    http_req_failed: ['rate<0.01'],          // < 1% error rate
    'ws_connecting': ['p(95)<500'],          // 95% of WS connects < 500ms
  },
};

export default function () {
  // 1. Health check (HTTP)
  const healthRes = http.get(`${BASE_URL}/health`);
  check(healthRes, {
    'health status 200': (r) => r.status === 200,
  });

  // 2. Connect via WebSocket
  const wsUrl = `${WS_URL}/table?table_id=test_load`;
  const res = ws.connect(wsUrl, {}, function (socket) {
    socket.on('open', () => {
      // 3. Send a simple message
      socket.send(JSON.stringify({
        type: 'ping',
      }));

      socket.on('message', (msg) => {
        // Just acknowledge
      });

      // Hold connection for a bit
      sleep(Math.random() * 3 + 1);
      socket.close();
    });

    socket.on('error', (e) => {
      console.error(`WS error: ${e}`);
    });
  });

  check(res, {
    'ws connect success': (r) => r && r.status === 101,
  });

  // 4. Random pause between iterations
  sleep(Math.random() * 2);
}

// Teardown: optional cleanup
export function teardown() {
  console.log('Load test completed.');
}
