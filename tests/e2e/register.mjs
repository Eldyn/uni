#!/usr/bin/env node
/**
 * End-to-end smoke test for the user-registration endpoint.
 *
 * Hits POST /auth/register on the running backend (Docker container or local
 * server) and asserts a 2xx response. Cross-platform: pure Node (>=18), no
 * dependencies. The transport (http/https) follows the BASE_URL scheme, so it
 * works against the plain-HTTP container, a TLS local build, or a Render URL.
 * With https, rejectUnauthorized:false accepts self-signed dev certificates.
 *
 * Usage:
 *   node tests/e2e/register.mjs                       # http://localhost:3000
 *   BASE_URL=https://localhost:9999 node tests/e2e/register.mjs
 *   BASE_URL=https://uni.onrender.com node tests/e2e/register.mjs
 */
import http from 'node:http';
import https from 'node:https';

const BASE_URL = process.env.BASE_URL ?? 'http://localhost:3000';

// Unique identity per run so re-runs don't collide on the UNIQUE constraints.
const stamp = Date.now().toString(36);
const payload = {
    username: `tester_${stamp}`.slice(0, 32),
    email: `tester_${stamp}@example.com`,
    password: 'sup3r-secret-pw',
    confirmPassword: 'sup3r-secret-pw',
};

function post(urlString, body) {
    const url = new URL(urlString);
    const client = url.protocol === 'https:' ? https : http;
    const data = JSON.stringify(body);
    const options = {
        method: 'POST',
        hostname: url.hostname,
        port: url.port,
        path: url.pathname,
        headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(data) },
        rejectUnauthorized: false, // accept self-signed dev cert (https only)
    };
    return new Promise((resolve, reject) => {
        const req = client.request(options, (res) => {
            let chunks = '';
            res.on('data', (c) => (chunks += c));
            res.on('end', () => resolve({ status: res.statusCode, body: chunks }));
        });
        req.on('error', reject);
        req.write(data);
        req.end();
    });
}

const url = `${BASE_URL}/auth/register`;
console.log(`POST ${url}`);
console.log(`  username=${payload.username} email=${payload.email}`);

try {
    const res = await post(url, payload);
    console.log(`→ HTTP ${res.status}`);
    console.log(`  body: ${res.body}`);

    if (res.status >= 200 && res.status < 300) {
        console.log('✅ PASS — registration succeeded');
        // Emit the created username so a caller can verify the DB row.
        console.log(`CREATED_USERNAME=${payload.username}`);
        process.exit(0);
    }
    console.error(`❌ FAIL — expected 2xx, got ${res.status}`);
    process.exit(1);
} catch (err) {
    console.error(`❌ FAIL — request error: ${err.message}`);
    process.exit(1);
}
