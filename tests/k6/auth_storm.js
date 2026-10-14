import http from 'k6/http';
import { check } from 'k6';
export const options = { vus: 50, duration: '60s', thresholds: { http_req_duration: ['p(95)<800'] } };
const BASE = __ENV.BASE_URL || 'http://localhost:3000';
export default function () {
  const r = http.post(`${BASE}/auth/login`, JSON.stringify({ email: 'bench@example.com', password: 'sup3r-secret-pw' }), { headers: { 'Content-Type': 'application/json' } });
  check(r, { 'login not 5xx': (x) => x.status < 500 });
}
