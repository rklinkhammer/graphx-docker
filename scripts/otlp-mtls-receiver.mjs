import { readFileSync, writeFileSync } from 'node:fs'
import { createServer } from 'node:https'

const token = readFileSync(process.env.GRAPHX_TEST_TOKEN_FILE, 'utf8').trim()
const server = createServer({
  key: readFileSync(process.env.GRAPHX_TEST_SERVER_KEY),
  cert: readFileSync(process.env.GRAPHX_TEST_SERVER_CERT),
  ca: readFileSync(process.env.GRAPHX_TEST_CA_FILE),
  requestCert: true,
  rejectUnauthorized: true,
}, (request, response) => {
  let body = ''
  request.setEncoding('utf8')
  request.on('data', chunk => { body += chunk })
  request.on('end', () => {
    try {
      if (!request.socket.authorized || request.headers.authorization !== `Bearer ${token}`)
        throw new Error('unauthorized test export')
      const payload = JSON.parse(body)
      if (!Array.isArray(payload.resourceMetrics)) throw new Error('expected OTLP metric request')
      writeFileSync(process.env.GRAPHX_TEST_RESULT_FILE, 'authorized OTLP metric export\n', { flag: 'wx' })
      response.writeHead(200, { 'content-type': 'application/json' }); response.end('{}')
    } catch {
      response.writeHead(400, { 'content-type': 'application/json' }); response.end('{}')
    }
  })
})

server.listen(Number(process.env.GRAPHX_TEST_PORT), '0.0.0.0', () => console.log('ready'))
const shutdown = () => server.close(() => process.exit(0))
process.on('SIGINT', shutdown)
process.on('SIGTERM', shutdown)
