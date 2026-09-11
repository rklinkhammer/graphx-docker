import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import test from 'node:test'
import { fileURLToPath } from 'node:url'

const directory = dirname(fileURLToPath(import.meta.url))
const serverSource = readFileSync(resolve(directory, 'server.mjs'), 'utf8')
const imageSource = readFileSync(resolve(directory, '../../docker/telemetry.Dockerfile'), 'utf8')

test('server remains a configuration and transport wiring boundary', () => {
  assert.match(serverSource, /createTelemetryCollector/)
  assert.match(serverSource, /createHttpRequestHandler/)
  assert.doesNotMatch(serverSource, /createMetricStore|new ReplayCache|new ControlPlane/)
  assert.doesNotMatch(serverSource, /function (snapshot|prometheus|handleDatagram|issueControl)/)
  assert.doesNotMatch(serverSource, /setInterval\(/)
  assert.ok(serverSource.split('\n').length <= 250,
    'server.mjs should not regain collector application responsibilities')
})

test('telemetry image packages every runtime module', () => {
  for (const module of ['collector.mjs', 'http-routes.mjs', 'metric-store.mjs',
    'runtime-evidence.mjs', 'topology.mjs']) assert.match(imageSource, new RegExp(module))
})
