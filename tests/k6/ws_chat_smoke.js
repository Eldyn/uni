import http from 'k6/http';
import ws from 'k6/ws';
import { check } from 'k6';
export const options = { vus: 20, duration: '30s', thresholds: { http_req_failed: ['rate<0.01'], http_req_duration: ['p(95)<500'] } };
const BASE = __ENV.BASE_URL || 'http://localhost:3000';
export default function () {
  const stamp = `${Date.now()}${__VU}${__ITER}`;
  const res = http.post(`${BASE}/auth/register`, JSON.stringify({ username: `k6_${stamp}`.slice(0, 32), email: `k6_${stamp}@ex.com`, password: 'sup3r-secret-pw', confirmPassword: 'sup3r-secret-pw' }), { headers: { 'Content-Type': 'application/json' } });
  check(res, { 'register 2xx': (r) => r.status >= 200 && r.status < 300 });
}
