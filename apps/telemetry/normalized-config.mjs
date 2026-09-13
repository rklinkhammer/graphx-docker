import Ajv2020 from 'ajv/dist/2020.js'
import { closeSync, constants, fstatSync, openSync, readSync, readFileSync } from 'node:fs'
import { dirname, normalize } from 'node:path'

export const MAX_NORMALIZED_CONFIG_BYTES = 4 * 1024 * 1024

function fail(message) {
  throw new Error(`GraphX telemetry configuration: ${message}`)
}

function readBoundedRegularFile(path, maximum) {
  let descriptor
  try {
    descriptor = openSync(path, constants.O_RDONLY | constants.O_NONBLOCK | (constants.O_NOFOLLOW || 0))
    const metadata = fstatSync(descriptor)
    if (!metadata.isFile()) fail(`${path} is not a regular file`)
    if (metadata.size > maximum) fail(`${path} exceeds the ${maximum}-byte limit`)
    const buffer = Buffer.alloc(metadata.size)
    let offset = 0
    while (offset < buffer.length) {
      const count = readSync(descriptor, buffer, offset, buffer.length - offset, null)
      if (count === 0) break
      offset += count
    }
    if (offset !== metadata.size) fail(`${path} changed while it was being read`)
    return buffer.toString('utf8')
  } catch (error) {
    if (error?.message?.startsWith('GraphX telemetry configuration:')) throw error
    fail(`cannot read ${path}: ${error.message}`)
  } finally {
    if (descriptor != null) closeSync(descriptor)
  }
}

const schema = JSON.parse(readFileSync(new URL('../../config/schema/normalized-graph.schema.json', import.meta.url), 'utf8'))
const validate = new Ajv2020({ allErrors: false, strictRequired: false, strictTypes: false }).compile(schema)

function validateNormalizedConfig(root) {
  if (root?.contract_version !== 2) fail('contract_version must be 2')
  if (!validate(root)) fail(`invalid normalized contract: ${validate.errors[0].instancePath} ${validate.errors[0].message}`)
  return root
}

// Presentation/runtime DTO derived only from the validated normalized contract.
// This does not accept authored YAML or a previous normalized version.
export function applicationGraph(config) {
  return {
    id: config.graph_id,
    nodes: config.nodes.map(node => ({
      id: node.node_id, kind: node.type,
      runtime: node.execution.kind === 'container' ? 'docker'
        : ['native', 'namespace'].includes(node.execution.kind) ? 'process' : node.execution.kind,
      execution: node.execution.kind,
      lifecycle: node.execution.kind === 'external' ? 'external' : 'managed',
      control: node.control, accelerator: node.execution.accelerator || null,
      architecture: node.execution.architecture || null,
      observation: node.observation,
      ports: Object.entries(node.bindings).map(([name, bindings]) => ({
        name, direction: bindings[0]?.role === 'listen' ? 'input' : 'output',
        schema: bindings[0]?.schema || 'unknown',
      })),
    })),
    edges: config.connections.map(connection => ({
      id: connection.id, from: connection.from, to: connection.to,
      transport: { kind: connection.transport, ...connection.settings },
      data_plane: connection.encoding === 'raw' ? 'external' : 'graphx',
    })),
  }
}

export function loadNormalizedConfig(path) {
  const contents = readBoundedRegularFile(path, MAX_NORMALIZED_CONFIG_BYTES)
  let parsed
  try { parsed = JSON.parse(contents) } catch (error) { fail(`malformed JSON in ${path}: ${error.message}`) }
  return validateNormalizedConfig(parsed)
}

export function loadTelemetryConfiguration({ environment = process.env } = {}) {
  const normalizedPath = environment.GRAPHX_NORMALIZED_CONFIG
  if (!normalizedPath) fail('GRAPHX_NORMALIZED_CONFIG is required')
  return {
    config: loadNormalizedConfig(normalizedPath),
    baseDirectory: normalize(environment.GRAPHX_CONFIG_DIRECTORY || dirname(normalizedPath)),
  }
}
