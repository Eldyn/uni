import http from 'k6/http';
import { check } from 'k6';
export const options = { vus: 30, duration: '60s', thresholds: { http_req_duration: ['p(95)<400'] } };
const BASE = __ENV.BASE_URL || 'http://localhost:3000';
export default function () { const r = http.get(`${BASE}/stats/leaderboard`); check(r, { 'lb 2xx': (x) => x.status === 200 }); }
