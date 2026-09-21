#include "graphx/compile.hpp"
#include "graphx/platform_assets.hpp"
#include "graphx/config_schemas.hpp"
#include "graphx/version.hpp"
#include "config_document.hpp"

#include <algorithm>
#include <regex>
#include <set>

namespace graphx {
namespace {
using namespace config_internal;
const Value empty{Object{}};
const Value& member(const Value& v, std::string_view key) {
  return v.contains(key) ? v.at(key) : empty;
}
Value strings(std::initializer_list<const char*> values) {
  Array result;
  for (const auto* value : values) result.emplace_back(value);
  return result;
}
void append(Array& a, std::initializer_list<std::string> values) {
  for (const auto& value : values) a.emplace_back(value);
}
void identity(const std::string& id) {
  static const std::regex pattern("^[a-z][a-z0-9_-]{0,63}$");
  if (!std::regex_match(id, pattern)) reject("E_COMPILE_ID", id, "unsafe artifact identity");
}
void image_pin(const Value& image) {
  static const std::regex pattern("^[a-zA-Z0-9][a-zA-Z0-9._/:-]*@sha256:[a-f0-9]{64}$");
  if (!image.is_string() || !std::regex_match(image.text(), pattern))
    reject("E_COMPILE_PIN", "catalog.image", "digest-pinned image required");
}
const Value& catalog_definition(const Value& catalog, std::string_view prefix,
                                const std::string& id) {
  for (const auto& [path, definition] : catalog.object())
    if (path.starts_with(prefix) && definition.at("id") == Value(id)) return definition;
  reject("E_COMPILE_CATALOG", id, "missing pinned catalog definition");
}
Array node_argv(const Value& node, const Value& type, bool container) {
  const auto id = node.at("node_id").text();
  const auto exe = type.at("executable").text();
  static const std::regex executable("^graphx-[a-z0-9-]+$");
  if (!std::regex_match(exe, executable) || type.at("template") != Value("node-v1"))
    reject("E_COMPILE_TEMPLATE", "type", "unsupported executable or template");
  Array argv;
  append(argv, {container ? exe : "${GX_RELEASE}/bin/" + exe, "--node", id, "--config",
                container ? "/run/graphx/node.json" : "${GX_OUTPUT}/nodes/" + id + ".json",
                "--release-file",
                container ? "/run/graphx/barriers/release" : "${GX_STATE}/barriers/release",
                "--release-token", "${GX_OWNER}"});
  return argv;
}
Value credential_plan(const GraphConfig& graph) {
  Object entries, consumers;
  const auto allow = [&](const std::string& ref, const std::string& node) {
    if (!consumers.contains(ref)) consumers[ref] = Array{};
    consumers[ref].array().emplace_back(node);
  };
  for (const auto& [id, declaration] : member(graph.authored, "credentials").object())
    entries[id] = declaration;
  if (entries.contains("observer"))
    reject("E_CREDENTIAL", "observer", "observer is a reserved runtime credential");
  entries["observer"] = Object{
      {"provider", "runtime-generated"}, {"identity", "reader"}, {"members", strings({"token"})}};
  allow("observer", "reader");
  for (const auto& node : graph.resolved.at("nodes").array()) {
    const auto id = node.at("node_id").text();
    for (const auto& [port, ref] : node.at("credentials").object()) {
      allow(ref.text(), id);
      if (entries.at(ref.text()).at("provider") == Value("lab-generated")) {
        auto& roles = entries.at(ref.text())["tls_roles"];
        if (roles.is_null()) roles = Array{};
        for (const auto& peer : node.at("bindings").at(port).array()) {
          const Value role = peer.at("role") == Value("listen") ? "serverAuth" : "clientAuth";
          if (std::ranges::find(roles.array(), role) == roles.array().end())
            roles.array().push_back(role);
        }
        std::ranges::sort(roles.array(),
                          [](const Value& a, const Value& b) { return a.text() < b.text(); });
      }
    }
    const auto& ref = node.at("telemetry").at("credential");
    if (!ref.is_null()) {
      if (entries.contains(ref.text()))
        reject("E_CREDENTIAL", ref.text(), "runtime credential collides with authored credential");
      entries[ref.text()] = Object{
          {"provider", "runtime-generated"}, {"identity", id}, {"members", strings({"hmac"})}};
      allow(ref.text(), id);
    }
  }
  for (auto& [id, declaration] : entries) {
    identity(id);
    Object files;
    for (const auto& part : declaration.at("members").array()) {
      const auto& name = part.text();
      if (name.empty() || name == "." || name == ".." ||
          name.find_first_of("/\\${}\n\r") != std::string::npos)
        reject("E_CREDENTIAL", id, "credential member must be a literal filename");
      files[name] = "${GX_CREDENTIALS}/" + id + "/" + name;
    }
    declaration["files"] = files;
    if (!consumers.contains(id)) consumers[id] = Array{};
    // Platform only receives runtime HMACs, the observer token, grants and OTLP.
    if (id == "observer" || declaration.at("provider") == Value("runtime-generated"))
      allow(id, "platform");
  }
  const auto& platform = graph.resolved.at("platform");
  for (const auto& extension : platform.at("extensions").array()) {
    if (extension == Value("prometheus")) allow("observer", "prometheus");
    if (extension == Value("grafana")) {
      if (!entries.contains("grafana-admin"))
        reject("E_CREDENTIAL", "grafana-admin", "Grafana requires an external password reference");
      allow("grafana-admin", "grafana");
    }
  }
  for (const auto& grant : platform.at("control").at("grants").array())
    allow(grant.at("credential").text(), "platform");
  if (platform.at("otlp").at("enabled").boolean())
    allow(platform.at("otlp").at("credentials").text(), "platform");
  for (auto& [id, list] : consumers) {
    (void)id;
    std::ranges::sort(list.array(), {}, [](const Value& v) { return v.text(); });
    auto unique = std::ranges::unique(list.array());
    list.array().erase(unique.begin(), unique.end());
  }
  return Object{
      {"version", 1},
      {"deny_inline_values", true},
      {"entries", entries},
      {"allowed_consumers", consumers},
      {"bundle_contract",
       Object{{"staging_mode", "0700"},
              {"directory_mode", "0500"},
              {"file_mode", "0400"},
              {"publication", "atomic member replacement inside stable credential directory"},
              {"rotation", "current and previous members with bounded monotonic grace"}}}};
}
Array credential_refs(const Value& plan, const std::string& consumer) {
  Array result;
  for (const auto& [id, consumers] : plan.at("allowed_consumers").object())
    if (std::ranges::find(consumers.array(), Value(consumer)) != consumers.array().end())
      result.emplace_back(id);
  return result;
}
Value service_base(const Value& image) {
  image_pin(image);
  return Object{{"image", image},
                {"restart", "no"},
                {"read_only", true},
                {"user", "65532:65532"},
                {"cap_drop", strings({"ALL"})},
                {"security_opt", strings({"no-new-privileges:true"})},
                {"tmpfs", strings({"/tmp:rw,nosuid,nodev,size=16m"})},
                {"pids_limit", 64},
                {"mem_limit", 134217728},
                {"logging", Object{{"driver", "json-file"},
                                   {"options", Object{{"max-file", "2"}, {"max-size", "1m"}}}}}};
}
Value node_environment(const Value& node, bool container) {
  const std::string root = container ? "/run/secrets" : "${GX_CREDENTIALS}";
  Value result = Object{{"GRAPHX_CREDENTIALS", root}};
  const auto& credential = node.at("telemetry").at("credential");
  if (!credential.is_null())
    result["GRAPHX_TELEMETRY_SHARED_SECRET_FILE"] = root + "/" + credential.text() + "/hmac";
  return result;
}
Value process(const std::string& id, Array argv, const Value& refs, const Value& readiness) {
  return Object{{"id", id},
                {"kind", "native"},
                {"argv", argv},
                {"cwd", "${GX_OUTPUT}"},
                {"environment", Object{}},
                {"credentials", refs},
                {"readiness", readiness},
                {"log", "${GX_STATE}/logs/" + id + ".log"},
                {"limits", Object{{"log_bytes", 2097152}, {"memory_bytes", 134217728}}}};
}
Value qemu_guest(const Value& node, const Value& recipe, const Value& network) {
  const auto id = node.at("node_id").text();
  const auto recipe_id = recipe.at("id").text();
  const Value* tap = nullptr;
  for (const auto& attachment : network.at("attachments").array())
    if (attachment.at("owner") == Value(id) && attachment.at("kind") == Value("qemu_tap")) {
      if (tap) reject("E_COMPILE_GUEST", id, "one TAP per guest is supported");
      tap = &attachment;
    }
  if (!tap || tap->at("tap_uid") != Value(65532) || tap->at("tap_gid") != Value(65532) ||
      recipe.at("architecture") != Value("x86_64") ||
      node.at("execution").at("accelerator") != Value("tcg"))
    reject("E_COMPILE_GUEST", id, "x86_64 TCG and one resolved TAP required");
  Array argv;
  append(argv, {"qemu-system-x86_64",
                "-nodefaults",
                "-no-user-config",
                "-machine",
                "q35,accel=tcg",
                "-m",
                "256",
                "-smp",
                "1",
                "-kernel",
                "${GX_RELEASE}/guests/" + recipe_id + "/bzImage",
                "-initrd",
                "${GX_RELEASE}/guests/" + recipe_id + "/rootfs.cpio.gz",
                "-append",
                "console=ttyS0 panic=1",
                "-no-reboot",
                "-display",
                "none",
                "-monitor",
                "none",
                "-qmp",
                "unix:${GX_STATE}/" + id + "/qmp.sock,server=on,wait=off",
                "-netdev",
                "tap,id=net0,ifname=" + tap->at("interface").text() + ",script=no,downscript=no",
                "-device",
                "virtio-net-pci,netdev=net0,mac=" + tap->at("mac").text(),
                "-device",
                "virtio-serial-pci"});
  Object channels;
  for (const auto* channel : {"config", "credentials", "ready"}) {
    const std::string socket = "${GX_STATE}/" + id + "/" + channel + ".sock";
    channels[channel] =
        Object{{"guest_name", "org.graphx." + std::string(channel)}, {"host_socket", socket}};
    append(
        argv,
        {"-chardev",
         "socket,id=" + std::string(channel) + ",path=" + socket + ",server=on,wait=off", "-device",
         "virtserialport,chardev=" + std::string(channel) + ",name=org.graphx." + channel});
  }
  append(argv, {"-chardev", "ringbuf,id=serial,size=1048576", "-serial", "chardev:serial"});
  append(argv, {"-chardev",
                "socket,id=interactive,path=${GX_STATE}/console/" + id + ".sock,server=on,wait=off",
                "-serial", "chardev:interactive"});
  return Object{{"node", id},
                {"recipe", recipe_id},
                {"argv", argv},
                {"architecture", "x86_64"},
                {"accelerator", "tcg"},
                {"uid", 65532},
                {"gid", 65532},
                {"node_config", "nodes/" + id + ".json"},
                {"config_delivery", "framed node JSON before application start"},
                {"credentials", node.at("credentials")},
                {"channels", channels},
                {"readiness", recipe.at("readiness")},
                {"artifact_verification", recipe.at("outputs")},
                {"network", *tap},
                {"serial_max_bytes", 1048576}};
}
}  // namespace

namespace config_internal {
Value compiled_guest_plan(const Value& node, const Value& recipe, const Value& network) {
  return qemu_guest(node, recipe, network);
}
}  // namespace config_internal

CompiledGraph compile_graph(const GraphConfig& graph) {
  using namespace config_internal;
  static const auto schema = parse_document(normalized_schema);
  validate_shape(graph.resolved, schema);
  if (graph.version != 3 || graph.id != graph.resolved.at("graph_id").text() ||
      !graph.catalog.contains("files"))
    reject("E_COMPILE_INPUT", "$", "a resolved v3 graph with verified catalog is required");
  identity(graph.id);
  const auto& catalog = graph.catalog.at("files");
  // These catalog documents are capabilities, not an arbitrary template language.
  for (const auto* file : {"templates.json", "sources.json", "targets.json"})
    if (!catalog.contains(file))
      reject("E_COMPILE_CATALOG", file, "required pinned catalog document missing");
  const auto& templates = catalog.at("templates.json");
  const auto expected_templates = parse_document(
      R"json({"version":1,"node-v1":{"allowed_slots":["executable","node.id","node.file"],"arbitrary_compose":false,"argv":["executable","--node","node.id","--config","node.file"],"security":{"cap_drop":["ALL"],"read_only":true,"restart":"no","security_opt":["no-new-privileges:true"],"user":"65532:65532"}},"platform-v1":{"allowed_slots":["platform.file"],"arbitrary_compose":false,"argv":["graphx-platform","--config","platform.file"]}})json");
  if (templates != expected_templates)
    reject("E_COMPILE_TEMPLATE", "templates", "unsupported fixed template contract");
  const auto& resolved = graph.resolved;
  const auto& nodes = resolved.at("nodes").array();
  const auto& platform = resolved.at("platform");
  image_pin(platform.at("image"));
  if (platform.at("template") != Value("platform-v1"))
    reject("E_COMPILE_TEMPLATE", "platform", "unsupported platform template");
  const bool native = platform.at("telemetry").at("host") == Value("127.0.0.1");
  const auto& network = resolved.at("network");
  const bool ovs = !network.at("switches").array().empty();
  const bool execution_available =
      (!native || !ovs) &&
      !std::ranges::any_of(
          network.at("attachments").array(),
          [](const auto& attachment) { return attachment.at("kind") == Value("external"); }) &&
      std::ranges::all_of(nodes, [&](const Value& node) {
        const auto& kind = node.at("execution").at("kind");
        return native ? kind == Value("native")
                      : (kind == Value("container") || kind == Value("external") ||
                         (ovs && (kind == Value("namespace") || kind == Value("qemu"))));
      });
  const auto& targets = catalog.at("targets.json");
  if (!targets.contains(resolved.at("target").text()))
    reject("E_COMPILE_TARGET", "target", "missing pinned target capabilities");
  const auto& capability = targets.at(resolved.at("target").text());
  if ((!native && !capability.at("docker").boolean()) ||
      (native && !capability.at("platform_native").boolean()) ||
      (ovs && !capability.at("ovs").boolean()))
    reject("E_COMPILE_TARGET", "target",
           "pinned target capabilities do not support the resolved plan");
  CompiledGraph result{graph.id, {}};
  const auto put = [&](const std::string& path, const Value& value) {
    result.files.emplace(path, config_value_json(value));
  };
  put("resolved.json", resolved);
  put("platform.json", platform);
  auto credentials = credential_plan(graph);
  put("credentials.json", credentials);
  Object services, networks, platform_networks;
  Array processes, guests, external;
  const auto& portable = resolved.at("portable_network");
  if (!portable.is_null())
    networks["data"] =
        Object{{"driver", "bridge"},
               {"internal", true},
               {"ipam", Object{{"config", Array{Object{{"subnet", portable.at("subnet")}}}}}}};
  const auto p_refs = credential_refs(credentials, "platform");
  if (native) {
    auto p = process(
        "platform",
        {Value("${GX_RELEASE}/bin/graphx-platform"), Value("--config"),
         Value("${GX_OUTPUT}/platform.json")},
        p_refs,
        Object{{"kind", "http"},
               {"url",
                "http://127.0.0.1:" + std::to_string(platform.at("console").at("port").integer()) +
                    "/api/ready"},
               {"timeout_ms", 30000}});
    p["limits"]["memory_bytes"] = 268435456;
    processes.push_back(p);
  }
  for (const auto& node : nodes) {
    const auto id = node.at("node_id").text();
    identity(id);
    const auto& type = catalog_definition(catalog, "types/", node.at("type").text());
    image_pin(type.at("image"));
    if (type.at("revision") != node.at("type_revision"))
      reject("E_COMPILE_TYPE", id, "type revision mismatch");
    put("nodes/" + id + ".json", node);
    const auto kind = node.at("execution").at("kind").text();
    const auto refs = credential_refs(credentials, id);
    if (kind == "container") {
      auto service = service_base(type.at("image"));
      if (node.contains("recorder")) service["cap_add"] = strings({"NET_RAW"});
      auto argv = node_argv(node, type, true);
      service["entrypoint"] = Array{argv.front()};
      argv.erase(argv.begin());
      service["command"] = argv;
      service["mem_limit"] = type.at("resources").at("memory_bytes");
      service["pids_limit"] = type.at("resources").at("pids");
      service["labels"] = Object{{"org.graphx.graph", graph.id},
                                 {"org.graphx.node", id},
                                 {"org.graphx.owner", "${GX_OWNER}"}};
      service["environment"] = node_environment(node, true);
      Array volumes = strings({}).array();
      append(volumes, {"./nodes/" + id + ".json:/run/graphx/node.json:ro",
                       "${GX_STATE}/barriers:/run/graphx/barriers:ro"});
      for (const auto& ref : refs)
        volumes.emplace_back("${GX_CREDENTIALS}/" + ref.text() + ":/run/secrets/" + ref.text() +
                             ":ro");
      if (node.at("capture").at("enabled").boolean() &&
          node.at("capture").at("provider") == Value("application"))
        volumes.emplace_back("${GX_STATE}/captures/" + id + ":/captures");
      service["volumes"] = volumes;
      Object attached;
      const auto mg = "mg-" + id;
      networks[mg] = Object{{"driver", "bridge"}, {"internal", true}};
      attached[mg] = Object{};
      platform_networks[mg] = Object{{"aliases", strings({"platform"})}};
      if (!portable.is_null())
        attached["data"] = Object{{"ipv4_address", portable.at("addresses").at(id)}};
      service["networks"] = attached;
      services[id] = service;
    } else if (kind == "native" || kind == "namespace") {
      auto p = process(id, node_argv(node, type, false), refs, node.at("readiness"));
      p["kind"] = kind;
      p["namespace"] = kind == "namespace"
                           ? Value(resource_name(graph.id, "namespace",
                                                 string_or(node.at("execution"), "namespace", id)))
                           : Value();
      p["limits"]["memory_bytes"] = type.at("resources").at("memory_bytes");
      p["environment"] = node_environment(node, false);
      processes.push_back(p);
    } else if (kind == "qemu") {
      const auto ref = node.at("execution").at("guest").text();
      identity(ref);
      const auto& recipe = catalog_definition(catalog, "guests/", ref);
      image_pin(recipe.at("builder_image"));
      if (!catalog.at("sources.json").at("entries").contains(recipe.at("source_ref").text()))
        reject("E_COMPILE_SOURCE", ref, "guest source reference is not pinned in the catalog");
      const auto& source =
          catalog.at("sources.json").at("entries").at(recipe.at("source_ref").text());
      const std::filesystem::path relative(source.at("relative_path").text());
      if (relative.empty() || relative.is_absolute())
        reject("E_COMPILE_SOURCE", ref, "relative source path required");
      for (const auto& part : relative)
        if (part == "." || part == ".." || part.string().find('\\') != std::string::npos)
          reject("E_COMPILE_SOURCE", ref, "source path escapes its declared root");
      put("guest-build/" + ref + ".json", recipe);
      guests.push_back(qemu_guest(node, recipe, network));
    } else if (kind == "external") {
      external.emplace_back(
          Object{{"node", id}, {"lifecycle", "untouched"}, {"readiness", node.at("readiness")}});
    } else
      reject("E_COMPILE_EXECUTION", id, "unsupported execution kind");
  }
  if (!native) {
    auto service = service_base(platform.at("image"));
    service["mem_limit"] = std::max<std::int64_t>(
        536870912, platform.at("history").at("max_database_bytes").integer() / 2 + 268435456);
    service["entrypoint"] = strings({"graphx-platform"});
    service["command"] = strings({"--config", "/run/graphx/platform.json"});
    service["environment"] = Object{{"GX_CREDENTIALS", "/run/secrets"},
                                    {"GX_STATE", "/var/lib/graphx"},
                                    {"GX_CONSOLE", "/run/graphx/console"},
                                    {"GX_OWNER", "${GX_OWNER}"}};
    Array volumes =
        strings({"./resolved.json:/run/graphx/resolved.json:ro",
                 "./platform.json:/run/graphx/platform.json:ro",
                 "./credentials.json:/run/graphx/credentials.json:ro",
                 "${GX_STATE}/console:/run/graphx/console:ro",
                 "${GX_STATE}/history:/var/lib/graphx/history", "${GX_STATE}/handoff:/captures:ro"})
            .array();
    for (const auto& ref : p_refs)
      volumes.emplace_back("${GX_CREDENTIALS}/" + ref.text() + ":/run/secrets/" + ref.text() +
                           ":ro");
    service["volumes"] = volumes;
    if (platform_networks.empty()) {
      networks["mg-platform"] = Object{{"driver", "bridge"}, {"internal", true}};
      platform_networks["mg-platform"] = Object{};
    }
    // Internal application networks have no published host ports. A separate
    // console bridge carries loopback HTTP publication and platform OTLP egress.
    networks["mg-console"] = Object{{"driver", "bridge"}};
    auto console_networks = platform_networks;
    console_networks["mg-console"] = Object{};
    service["networks"] = console_networks;
    service["ports"] =
        Array{Value(platform.at("console").at("bind").text() + ":" +
                    std::to_string(platform.at("console").at("port").integer()) + ":" +
                    std::to_string(platform.at("console").at("port").integer()))};
    services["platform"] = service;
    for (const auto& extension : platform.at("extensions").array()) {
      const auto& id = extension.text();
      // Reviewed fixed extension recipes; never accept arbitrary Compose service data.
      auto extra = service_base(
          id == "prometheus"
              ? Value("prom/"
                      "prometheus:v3.13.0@sha256:"
                      "c6b27ea434f8389bfe233fbc7be381cf50587c286e871bc842008f5a1b1908a7")
              : Value("grafana/"
                      "grafana:13.2.0@sha256:"
                      "3fd54ae1214669f8355f065ec9f6445d5279a3d77095ab048ca045685272429b"));
      extra["networks"] = console_networks;
      extra["mem_limit"] = 536870912;
      extra["pids_limit"] = 128;
      if (id == "prometheus") {
        put("prometheus-alerts.json", parse_document(kPrometheusAlerts));
        const auto endpoint =
            "platform:" + std::to_string(platform.at("console").at("port").integer());
        const Object target{{"targets", Array{Value(endpoint)}}};
        const Object scrape{
            {"job_name", "graphx"},
            {"authorization", Object{{"credentials_file", "/run/secrets/observer/token"}}},
            {"static_configs", Array{Value(target)}}};
        put("prometheus.json", Object{{"rule_files", strings({"/etc/prometheus/alerts.json"})},
                                      {"global", Object{{"scrape_interval", "5s"}}},
                                      {"scrape_configs", Array{Value(scrape)}}});
        extra["command"] = strings(
            {"--config.file=/etc/prometheus/graphx.json", "--storage.tsdb.path=/prometheus/data",
             "--storage.tsdb.retention.time=24h", "--storage.tsdb.retention.size=128MB"});
        extra["volumes"] = strings({"./prometheus.json:/etc/prometheus/graphx.json:ro",
                                    "./prometheus-alerts.json:/etc/prometheus/alerts.json:ro",
                                    "${GX_CREDENTIALS}/observer:/run/secrets/observer:ro"});
        extra["tmpfs"] =
            strings({"/prometheus:rw,nosuid,nodev,noexec,size=256m,uid=65532,gid=65532,mode=0700"});
        extra["ports"] = strings({"127.0.0.1:9090:9090"});
      } else {
        extra["pids_limit"] = 256;
        put("grafana-dashboard.json", parse_document(kGrafanaDashboard));
        const Object provider{
            {"name", "GraphX"},  {"folder", "GraphX"},
            {"type", "file"},    {"disableDeletion", true},
            {"editable", false}, {"options", Object{{"path", "/etc/graphx-dashboards"}}}};
        put("grafana-dashboards.json",
            Object{{"apiVersion", 1}, {"providers", Array{Value(provider)}}});
        put("grafana-datasources.json",
            Object{{"apiVersion", 1},
                   {"datasources", Array{Object{{"name", "GraphX"},
                                                {"type", "prometheus"},
                                                {"access", "proxy"},
                                                {"url", "http://prometheus:9090"},
                                                {"isDefault", true}}}}});
        extra["environment"] =
            Object{{"GOMAXPROCS", "2"},
                   {"GF_SECURITY_ADMIN_PASSWORD__FILE", "/run/secrets/grafana-admin/password"},
                   {"GF_USERS_ALLOW_SIGN_UP", "false"},
                   {"GF_AUTH_ANONYMOUS_ENABLED", "false"}};
        extra["volumes"] = strings(
            {"./grafana-datasources.json:/etc/grafana/provisioning/datasources/graphx.yaml:ro",
             "./grafana-dashboards.json:/etc/grafana/provisioning/dashboards/graphx.yaml:ro",
             "./grafana-dashboard.json:/etc/graphx-dashboards/graphx.json:ro",
             "${GX_CREDENTIALS}/grafana-admin:/run/secrets/grafana-admin:ro"});
        extra["tmpfs"] =
            strings({"/tmp:rw,nosuid,nodev,size=16m",
                     "/var/lib/grafana:rw,nosuid,nodev,size=128m,uid=65532,gid=65532,mode=0700"});
        extra["ports"] = strings({"127.0.0.1:3000:3000"});
      }
      services[id] = extra;
    }
  }
  if (!services.empty()) {
    // JSON is a YAML 1.2 subset: one canonical serializer, quoted scalars, no aliases.
    put("compose.yaml", Object{{"name", graph.id}, {"services", services}, {"networks", networks}});
  }
  if (!processes.empty()) {
    std::ranges::sort(processes, {}, [](const Value& p) { return p.at("id").text(); });
    put("native-plan.json",
        Object{{"version", 1},
               {"owner_api", "owned-process-adapter"},
               {"processes", processes},
               {"start", "platform-ready then bind-all then release-connectors"},
               {"state", "existing ownership store"}});
  }
  if (!guests.empty())
    put("qemu-plan.json",
        Object{{"version", 1}, {"adapter", "qemu-process-adapter"}, {"guests", guests}});
  if (ovs) {
    put("ovs-plan.json",
        Object{{"version", 1},
               {"api", "execute_ovs_lifecycle"},
               {"config", "resolved.json"},
               {"resource_intent", network},
               {"capture_process_count",
                static_cast<std::int64_t>(network.at("captures").array().size())},
               {"privileged_authorization_required", true},
               {"cleanup", "identity-checked destroy; external devices untouched"}});
    Array captures;
    for (const auto& capture : network.at("captures").array())
      captures.emplace_back(
          Object{{"id", capture.at("id")},
                 {"destination", "${GX_STATE}/handoff/" + capture.at("id").text() + ".pcapng"},
                 {"interval_seconds", 5},
                 {"max_bytes", capture.at("max_file_bytes")}});
    if (!captures.empty())
      put("capture-handoff.json",
          Object{{"version", 1},
                 {"api", "export_owned_network_capture"},
                 {"captures", captures},
                 {"permissions",
                  "sealed read-only snapshots to platform group; live rings remain privileged"}});
  }
  if (graph.authored.contains("scenario")) put("scenario-plan.json", scenario_plan(graph));
  if (!graph.laboratory_action.empty())
    put("laboratory-selection.json", Object{{"version", 1},
                                            {"action", graph.laboratory_action},
                                            {"physical_device", false},
                                            {"trust", "isolated laboratory credentials"}});
  Array stages{Object{{"operation", "preflight"},
                      {"checks", strings({"manifest hashes", "verified release identities",
                                          "target capabilities", "roots and listener availability",
                                          "credential references"})}},
               Object{{"operation", "stage-credentials"}, {"plan", "credentials.json"}},
               Object{{"operation", "start-platform"},
                      {"owner", native ? "native-plan.json" : "compose.yaml"}},
               Object{{"operation", "await-platform"}, {"timeout_ms", 30000}}};
  if (std::ranges::any_of(nodes, [](const Value& node) {
        return node.at("execution").at("kind") == Value("namespace");
      }))
    stages.emplace_back(Object{{"operation", "prepare-owned-namespaces"},
                               {"owner", "existing namespace resource lifecycle"},
                               {"plan", "native-plan.json"}});
  stages.emplace_back(Object{{"operation", "start-applications-held"}});
  if (ovs) stages.emplace_back(Object{{"operation", "realize-ovs"}, {"plan", "ovs-plan.json"}});
  if (!guests.empty())
    stages.emplace_back(Object{{"operation", "start-guests-held"}, {"plan", "qemu-plan.json"}});
  stages.emplace_back(Object{{"operation", "await-local-listeners"},
                             {"timeout_ms", resolved.at("lifecycle").at("readiness_ms")},
                             {"startup", resolved.at("lifecycle").at("startup")},
                             {"protocol", "ready node=ID on owned process stdout"}});
  stages.emplace_back(Object{{"operation", "release-connectors"},
                             {"file", "${GX_STATE}/barriers/release"},
                             {"contents", "${GX_OWNER}"},
                             {"publication", "atomic rename"}});
  stages.emplace_back(Object{{"operation", "return"}, {"continuous_supervision", false}});
  put("execution-plan.json",
      Object{{"version", 1},
             {"config", "resolved.json"},
             {"mode", native ? "native" : "compose"},
             {"executable", execution_available},
             {"stages", stages},
             {"lifecycle", resolved.at("lifecycle")},
             {"external", external},
             {"scenario_actions", "never implicit"},
             {"on_failure",
              "classified application unavailability follows lifecycle policy; other failures "
              "reverse newly owned resources and retain evidence"},
             {"stop_order", strings({"applications and guests", "capture handoff", "OVS resources",
                                     "platform", "ephemeral credentials"})}});
  Object substitutions;
  for (const auto* key : {"GX_OUTPUT", "GX_STATE", "GX_CREDENTIALS", "GX_RELEASE", "GX_OWNER"})
    substitutions[key] = Object{
        {"type", std::string_view(key) == "GX_OWNER" ? "identity-token" : "absolute-directory"},
        {"phase", "execution"}};
  put("substitutions.json",
      Object{{"syntax", "${GX_NAME}"}, {"secrets_in_values", false}, {"values", substitutions}});
  put("catalog-pins.json",
      Object{{"catalog_digest", resolved.at("catalog_digest")},
             {"lock", graph.catalog.at("lock")},
             {"artifacts_verified", false},
             {"verification_gate", "P4 images and native release; P8 guest artifacts"},
             {"documents", catalog}});
  Array hashes;
  for (const auto& [path, bytes] : result.files)
    hashes.emplace_back(Object{{"path", path}, {"sha256", sha256(bytes)}});
  put("compile-manifest.json",
      Object{{"version", 1},
             {"compiler", std::string(kCompilerContract)},
             {"compiler_version", std::string(version)},
             {"serializer", "graphx-json-1-yaml12-subset"},
             {"graph_id", graph.id},
             {"target", resolved.at("target")},
             {"catalog_digest", resolved.at("catalog_digest")},
             {"input_digest", resolved.at("input_digest")},
             {"files", hashes},
             {"execution_available", execution_available},
             {"artifact_identities_verified", false},
             {"replacement",
              "refused; use fresh output until owned-consumer inactivity can be verified"}});
  return result;
}
}  // namespace graphx
