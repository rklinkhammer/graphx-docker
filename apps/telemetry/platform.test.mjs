import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, realpathSync, mkdirSync, readFileSync, writeFileSync, chmodSync,
  rmSync, symlinkSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
import { spawn, execFileSync } from 'node:child_process'
import { once } from 'node:events'
import { stageCredentials, CredentialReader, rotateCredential } from './credentials.mjs'
import { loadPlatform, PlatformCredentials } from './platform-config.mjs'

const repository = resolve(import.meta.dirname, '../..')
const cli = process.env.GRAPHX_CLI || join(repository, 'build/dev/graphx')
const temporary = () => mkdtempSync(join(realpathSync(tmpdir()), 'graphx-platform-'))
const manifest = { version: 1, deny_inline_values: true, entries: {
  observer: { provider: 'runtime-generated', identity: 'reader', members: ['token'] },
  first: { provider: 'runtime-generated', identity: 'node', members: ['hmac'] },
  next: { provider: 'runtime-generated', identity: 'node', members: ['hmac'] },
}, allowed_consumers: { observer: ['platform', 'reader'], first: ['node', 'platform'], next: ['node', 'platform'] } }
function cleanup(root) { execFileSync('chmod', ['-R', 'u+w', root]); rmSync(root, { recursive: true, force: true }) }

test('staging is explicit, scoped, distinct, exclusive and rejects unsafe inputs', () => {
  const root = temporary(), destination = join(root, 'credentials')
  try {
    stageCredentials(manifest, destination)
    const reader = new CredentialReader(destination, manifest, 'platform')
    assert.notDeepEqual(reader.read('first').members.hmac, reader.read('next').members.hmac)
    assert.throws(() => stageCredentials(manifest, destination), /exist/)
    assert.throws(() => new CredentialReader(destination, manifest, 'reader').read('first'), /unavailable/)
    const directory = join(destination, 'first'); chmodSync(directory, 0o700)
    rmSync(join(directory, 'hmac')); symlinkSync(join(destination, 'next/hmac'), join(directory, 'hmac'))
    assert.throws(() => reader.read('first'), /symbolic/)
  } finally { cleanup(root) }
})
test('rotation keeps stable directories, rejects incomplete/stale generations and expires monotonically', () => {
  const root = temporary(), destination = join(root, 'credentials')
  try {
    stageCredentials(manifest, destination)
    let monotonic = 0, wall = Date.now()
    const reader = new CredentialReader(destination, manifest, 'platform', { now: () => monotonic, wall: () => wall })
    const original = reader.read('first').members.hmac
    const oldMetadata = readFileSync(join(destination, 'first/generation.json'))
    rotateCredential(manifest, destination, 'first', 'next', 1)
    assert.deepEqual(reader.read('first').previous.hmac, original)
    wall -= 3600000; monotonic = 1001
    assert.equal(reader.read('first').previous.hmac, undefined)
    chmodSync(join(destination, 'first/generation.json'), 0o600)
    writeFileSync(join(destination, 'first/generation.json'), oldMetadata)
    assert.throws(() => reader.read('first'), /stale/)
  } finally { cleanup(root) }
})
test('external provisioning never generates replacement trust; lab identities have distinct keys and one CA', () => {
  const root = temporary()
  try {
    const lab = { version: 1, deny_inline_values: true, entries: Object.fromEntries(['radio','processor'].map(id =>
      [id, { provider: 'lab-generated', identity: id, tls_roles: [id === 'radio' ? 'serverAuth' : 'clientAuth'], members: ['ca.pem','cert.pem','key.pem'] }])),
    allowed_consumers: { radio: ['radio'], processor: ['processor'] } }
    stageCredentials(lab, join(root, 'lab'))
    const radio = new CredentialReader(join(root, 'lab'), lab, 'radio').read('radio').members
    const processor = new CredentialReader(join(root, 'lab'), lab, 'processor').read('processor').members
    assert.deepEqual(radio['ca.pem'], processor['ca.pem'])
    assert.notDeepEqual(radio['key.pem'], processor['key.pem'])
    assert.match(execFileSync('openssl', ['x509', '-noout', '-text'], { input: radio['cert.pem'], encoding:'utf8' }), /DNS:radio/)
    lab.entries.radio.provider = 'external'
    assert.throws(() => stageCredentials(lab, join(root, 'external')), /requires an input directory/)
  } finally { cleanup(root) }
})

function compile(root, graph, target = 'native-macos') {
  const output = join(root, 'compiled')
  execFileSync(cli, ['compile', join(repository, graph), '--output', output, '--source-root', repository,
    '--credential-root', join(root, 'credentials'), '--target', target])
  return output
}
const sleep = ms => new Promise(resolve_ => setTimeout(resolve_, ms))
async function ready(port, child) {
  for (let i = 0; i < 100; i++) {
    if (child.exitCode != null) throw new Error(`platform exited ${child.exitCode}`)
    try { if ((await fetch(`http://127.0.0.1:${port}/api/ready`)).ok) return } catch {}
    await sleep(50)
  }
  throw new Error('platform readiness timeout')
}
async function stop(child) { if (child.exitCode == null) { child.kill('SIGTERM'); await once(child, 'exit') } }
test('compiled native platform requires read auth, preserves history, and deletes only an inactive matching owner', async () => {
  const root = temporary(); let child; let logs = ''; const applications = []
  try {
    const output = compile(root, 'examples/shared-memory/graphx.yml')
    const file = join(output, 'platform.json'), normalized = join(output, 'resolved.json')
    const platform = JSON.parse(readFileSync(file)), config = JSON.parse(readFileSync(normalized))
    platform.console.port = 18983; platform.control.allowed_origins = ['http://127.0.0.1:18983']
    platform.telemetry.port = 19983
    config.platform = platform
    writeFileSync(file, JSON.stringify(platform)); writeFileSync(normalized, JSON.stringify(config))
    stageCredentials(JSON.parse(readFileSync(join(output, 'credentials.json'))), join(root, 'credentials'))
    const env = { ...process.env, GRAPHX_CLI: cli, GX_CREDENTIALS: join(root, 'credentials'),
      GX_STATE: join(root, 'state'), GX_OWNER: 'test-platform-owner-12345678901234567890' }
    const loaded = loadPlatform(file, env), credentials = new PlatformCredentials(loaded)
    assert.equal(credentials.runtime.identities.size, 3)
    const launch = () => { const result = spawn(process.execPath, [join(repository, 'apps/telemetry/platform.mjs'), '--config', file], {env, stdio:['ignore','pipe','pipe']}); result.stdout.on('data', data => logs += data); result.stderr.on('data', data => logs += data); return result }
    child = launch(); await ready(18983, child)
    assert.equal((await fetch('http://127.0.0.1:18983/api/topology')).status, 401)
    const headers = { authorization: `Bearer ${credentials.observation[0]}` }
    assert.equal((await fetch('http://127.0.0.1:18983/api/topology', {headers})).status, 200)
    assert.equal((await fetch('http://127.0.0.1:18983/api/topology', {headers:{...headers,origin:'https://wrong.test'}})).status, 401)
    const release = join(root, 'release')
    for (const [id, binary] of [['generator','graphx-generator'], ['transform','graphx-transform'], ['sink','graphx-sink']]) {
      const path = join(output, 'nodes', id + '.json')
      const node = JSON.parse(readFileSync(path)); node.telemetry.port = platform.telemetry.port
      for (const peers of Object.values(node.bindings)) for (const binding of peers)
        if (binding.transport === 'shared_memory') binding.settings.segment += '-' + process.pid
      writeFileSync(path, JSON.stringify(node))
      const process_ = spawn(join(repository, 'build/dev', binary), ['--node', id, '--config', path,
        '--release-file', release, '--release-token', env.GX_OWNER], { env: {...env,
          GRAPHX_CREDENTIALS:env.GX_CREDENTIALS,
          GRAPHX_TELEMETRY_SHARED_SECRET_FILE:join(env.GX_CREDENTIALS, 'runtime-' + id, 'hmac')},
        stdio:['ignore','pipe','pipe'] })
      const app = {process:process_, output:''}; applications.push(app)
      process_.stdout.on('data', data => { app.output += data }); process_.stderr.on('data', data => {app.output += data})
    }
    for (let attempt=0; attempt<100 && applications.some(app=>!app.output.includes('ready node=')); attempt++) await sleep(30)
    assert.ok(applications.every(app=>app.output.includes('ready node=')), applications.map(app=>app.output).join('\n'))
    writeFileSync(release, env.GX_OWNER)
    for (let attempt=0; attempt<300 && applications.some(app=>app.process.exitCode == null); attempt++) await sleep(30)
    assert.ok(applications.every(app=>app.process.exitCode === 0), applications.map(app=>app.output).join('\n'))
    assert.match(applications[2].output, /seq=20 value=40/)
    const observed = await (await fetch('http://127.0.0.1:18983/api/topology', {headers})).json()
    assert.ok(observed.nodes.generator.lastSeen > 0)
    assert.ok(observed.nodes.sink.lastSeen > 0)
    const remove = owner => execFileSync(process.execPath, [join(repository, 'apps/telemetry/platform.mjs'),
      'history-remove', '--config', file, '--owner', owner], {env, stdio:'pipe'})
    assert.throws(() => remove(env.GX_OWNER), /Command failed/)
    await stop(child)
    child = launch(); await ready(18983, child); await stop(child)
    assert.throws(() => remove('wrong-owner-123456'), /Command failed/)
    remove(env.GX_OWNER)
  } catch (error) { error.message += `\n${logs}`; throw error }
  finally { for (const app of applications) await stop(app.process); if (child) await stop(child); cleanup(root) }
})

test('S01, S11 and V01-V06 consume compiled reference policies without source overlays', () => {
  const cases = ['scenarios/sample-pipeline', 'scenarios/network-observation',
    ...['history','observability','control','credential-rotation','secure-otlp','otlp-mtls'].map(name => 'variants/' + name)]
  for (const name of cases) {
    const root = temporary()
    try {
      const output = join(root, 'compiled')
      execFileSync(cli, ['compile', join(repository, 'design/graph-generation', name, 'graphx.yml'),
        '--output', output, '--source-root', repository, '--credential-root', join(root, 'credentials'), '--target', 'lima', '--catalog-root', join(repository, 'design/graph-generation/catalog')])
      const references = JSON.parse(readFileSync(join(output, 'credentials.json')))
      const external = join(root, 'external'); mkdirSync(external)
      // Explicit test fixture trust, never substituted for a real device at runtime.
      const fixture = { version: 1, deny_inline_values: true, entries: { test: {
        provider: 'lab-generated', identity: 'platform', members: ['ca.pem','cert.pem','key.pem'], tls_roles: ['clientAuth'] } },
      allowed_consumers: {test:['platform']} }
      stageCredentials(fixture, join(root, 'fixture'))
      for (const [ref, entry] of Object.entries(references.entries)) if (entry.provider === 'external') {
        const directory = join(external, ref); mkdirSync(directory, {mode:0o700})
        for (const member of entry.members) writeFileSync(join(directory, member), ['token', 'password'].includes(member)
          ? Buffer.from(`${ref}-${'t'.repeat(64)}`) : readFileSync(join(root, 'fixture/test', member)), {mode:0o400})
      }
      stageCredentials(references, join(root, 'credentials'), {externalRoot:external})
      const loaded = loadPlatform(join(output, 'platform.json'), {GX_CREDENTIALS:join(root,'credentials'), GX_STATE:join(root,'state')})
      const credentials = new PlatformCredentials(loaded)
      if (name.endsWith('network-observation')) assert.equal(credentials.runtime.identities.size, 0)
      if (name.endsWith('control') || name.endsWith('credential-rotation')) {
        const token = readFileSync(join(root,'credentials/operator/token'), 'utf8')
        const principal = credentials.control.authenticate(`Bearer ${token}`)
        assert.ok(credentials.control.permits(principal, 'pause', ['generator']))
        assert.ok(!credentials.control.permits(principal, 'pause', ['sink']))
        assert.ok(!credentials.control.permits(principal, 'reset', ['generator']))
      }
      if (name.endsWith('credential-rotation')) {
        const original = credentials.control.principals[0].token
        rotateCredential(references, join(root, 'credentials'), 'operator', 'operator-next', 0)
        assert.ok(credentials.reload())
        assert.equal(credentials.control.authenticate(`Bearer ${original}`), null)
        assert.ok(credentials.control.authenticate(`Bearer ${credentials.control.principals[0].token}`))
      }
      if (name.endsWith('secure-otlp')) assert.ok(credentials.otlp().token.length >= 32)
      if (name.endsWith('otlp-mtls')) assert.ok(credentials.otlp().key.length > 100)
    } finally { cleanup(root) }
  }
})

test('compiled control uses per-node HMAC, replay protection, scoped grants, idempotency, rotation and durable audit', async () => {
  const {createSocket} = await import('node:dgram')
  const {signEnvelope,verifyEnvelope,ReplayCache} = await import('./security.mjs')
  const {DatabaseSync} = await import('node:sqlite')
  const root=temporary(), udp=createSocket('udp4'); let child; let logs=''
  try {
    const output=compile(root,'examples/variants/credential-rotation/graphx.yml','orbstack')
    const file=join(output,'platform.json'), resolved=join(output,'resolved.json')
    const platform=JSON.parse(readFileSync(file)), config=JSON.parse(readFileSync(resolved))
    platform.control.grants.push({credential:'operator',nodes:['collector'],actions:['reset']})
    platform.console.port=18984;platform.control.allowed_origins=['http://127.0.0.1:18984']
    platform.telemetry.port=19984;platform.telemetry.host='127.0.0.1'
    platform.history.database_file=join(root,'history/history.sqlite')
    platform.history.batch_size=1;platform.history.flush_interval_ms=10
    config.platform=platform;writeFileSync(file,JSON.stringify(platform));writeFileSync(resolved,JSON.stringify(config))
    const manifest=JSON.parse(readFileSync(join(output,'credentials.json'))), external=join(root,'external')
    mkdirSync(external)
    for (const ref of ['operator','operator-next']) {mkdirSync(join(external,ref));writeFileSync(join(external,ref,'token'),ref+'x'.repeat(64),{mode:0o400})}
    const credentials=join(root,'credentials');stageCredentials(manifest,credentials,{externalRoot:external})
    const env={...process.env,GRAPHX_CLI:cli,GX_CREDENTIALS:credentials,GX_OWNER:'test-platform-control-owner-1234567890',GX_STATE:join(root,'state')}
    child=spawn(process.execPath,[join(repository,'apps/telemetry/platform.mjs'),'--config',file],{env,stdio:['ignore','pipe','pipe']})
    child.stdout.on('data',data=>logs+=data);child.stderr.on('data',data=>logs+=data)
    await ready(18984,child)
    await once(udp.bind(0,'127.0.0.1'),'listening')
    const secret=readFileSync(join(credentials,'runtime-generator/hmac'),'utf8')
    const observer=readFileSync(join(credentials,'observer/token'),'utf8'), operator=readFileSync(join(credentials,'operator/token'),'utf8')
    const replay=new ReplayCache()
    udp.on('message',data=>{const command=verifyEnvelope(JSON.parse(data),secret,replay);if(command?.kind==='control')
      udp.send(JSON.stringify(signEnvelope({kind:'control_ack',nodeId:'generator',action:command.action,
        commandId:command.commandId,accepted:true,state:'paused'},secret)),19984,'127.0.0.1')})
    const send=value=>udp.send(JSON.stringify(value),19984,'127.0.0.1')
    const event={kind:'trace',event:'heartbeat',nodeId:'generator',timestamp:Date.now(),cpuPercent:1}
    const snapshot=async()=>await (await fetch('http://127.0.0.1:18984/api/topology',{headers:{authorization:`Bearer ${observer}`}})).json()
    send(signEnvelope({...event,nodeId:'transform'},secret));send(signEnvelope(event,secret,Date.now()-60000));await sleep(40)
    assert.ok(!(await snapshot()).nodes.generator.lastSeen);assert.ok(!(await snapshot()).nodes.transform.lastSeen)
    const envelope=signEnvelope(event,secret);send(envelope);await sleep(40)
    const seen=(await snapshot()).nodes.generator.lastSeen;assert.ok(seen>0)
    send(envelope);await sleep(40);assert.equal((await snapshot()).nodes.generator.lastSeen,seen)
    const command=(token,action='pause',origin='http://127.0.0.1:18984')=>fetch('http://127.0.0.1:18984/api/control/commands',{
      method:'POST',headers:{authorization:`Bearer ${token}`,'content-type':'application/json',origin,'idempotency-key':'p5-pause'},
      body:JSON.stringify({action,targetNodes:['generator'],reason:'P5 verification'})})
    assert.equal((await command(operator,'pause','https://wrong.test')).status,403)
    const first=await command(operator);assert.equal(first.status,202);const issued=await first.json()
    const again=await (await command(operator)).json();assert.equal(again.replayed,true);assert.equal(again.command.id,issued.command.id)
    assert.equal((await command(operator,'resume')).status,409)
    const reset=token=>fetch('http://127.0.0.1:18984/api/control/commands',{
      method:'POST',headers:{authorization:`Bearer ${token}`,'content-type':'application/json',
        origin:'http://127.0.0.1:18984'},body:JSON.stringify({action:'reset'})})
    assert.equal((await reset(observer)).status,401)
    const resetResult=await reset(operator);assert.equal(resetResult.status,200)
    assert.equal((await resetResult.json()).accepted,true)
    const origin='http://127.0.0.1:18984'
    const handoff=await fetch(origin+'/api/console/handoff',{method:'POST',headers:{
      authorization:`Bearer ${observer}`,origin,'content-type':'application/json'},
      body:JSON.stringify({control_token:operator})})
    assert.equal(handoff.status,201)
    const {code}=await handoff.json()
    const exchange=()=>fetch(origin+'/api/console/session',{method:'POST',headers:{origin,
      'content-type':'application/json'},body:JSON.stringify({code})})
    const login=await exchange();assert.equal(login.status,200)
    const session=await login.json(), cookie=login.headers.get('set-cookie').split(';')[0]
    assert.equal(session.control,true)
    assert.ok(!JSON.stringify(session).includes(operator))
    assert.match(login.headers.get('set-cookie'),/HttpOnly; SameSite=Strict/)
    assert.equal((await exchange()).status,401)
    assert.equal((await fetch(origin+'/api/topology',{headers:{cookie}})).status,200)
    assert.equal((await fetch(origin+'/api/console/session',{headers:{cookie}})).status,200)
    const cookieReset=extra=>fetch(origin+'/api/control/commands',{method:'POST',headers:{cookie,
      origin,'content-type':'application/json',...extra},body:JSON.stringify({action:'reset'})})
    assert.equal((await cookieReset({})).status,401)
    assert.equal((await cookieReset({'x-graphx-csrf':session.csrf,origin:'http://evil.test'})).status,403)
    assert.equal((await cookieReset({'x-graphx-csrf':session.csrf})).status,200)
    const {WebSocket}=await import('ws')
    const websocket=new WebSocket(origin.replace('http:','ws:')+'/ws',['graphx'],{headers:{cookie,origin}})
    const firstSnapshot=await once(websocket,'message')
    assert.equal(JSON.parse(firstSnapshot[0]).graph,config.graph_id)
    websocket.close();await once(websocket,'close')
    rotateCredential(manifest,credentials,'operator','operator-next',0)
    assert.equal((await fetch(origin+'/api/topology',{headers:{cookie}})).status,401)

    assert.equal((await command(operator)).status,401)
    await sleep(100);await stop(child)
    const db=new DatabaseSync(platform.history.database_file,{readOnly:true})
    try {assert.ok(db.prepare("SELECT count(*) AS n FROM history_records WHERE kind='control_audit'").get().n>0)} finally {db.close()}
  } catch(error) {error.message+='\n'+logs;throw error}
  finally {udp.close();if(child)await stop(child);cleanup(root)}
})
