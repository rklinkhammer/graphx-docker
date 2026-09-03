-- GraphX canonical framed-envelope dissector for LINKTYPE_USER0 captures.
-- Supports envelope wire versions 1 and 2 without changing the wire format.

local graphx = Proto("graphx", "GraphX Framed Envelope")

local fields = {
    frame_length = ProtoField.uint32("graphx.frame_length", "Envelope length", base.DEC),
    magic = ProtoField.string("graphx.magic", "Magic"),
    version = ProtoField.uint8("graphx.version", "Wire version", base.DEC),
    sequence = ProtoField.uint64("graphx.sequence", "Sequence", base.DEC),
    timestamp = ProtoField.int64("graphx.timestamp_ns", "Timestamp (ns)", base.DEC),
    message_id = ProtoField.bytes("graphx.message_id", "Message ID"),
    trace_id = ProtoField.bytes("graphx.trace_id", "Trace ID"),
    parent_message_id = ProtoField.bytes("graphx.parent_message_id", "Parent message ID"),
    type_length = ProtoField.uint32("graphx.type_length", "Type length", base.DEC),
    message_type = ProtoField.bytes("graphx.type", "Type"),
    legacy_trace_length = ProtoField.uint32("graphx.legacy_trace_length", "Legacy trace length", base.DEC),
    legacy_trace_id = ProtoField.bytes("graphx.legacy_trace_id", "Legacy trace ID"),
    attribute_count = ProtoField.uint32("graphx.attribute_count", "Attribute count", base.DEC),
    attribute_key_length = ProtoField.uint32("graphx.attribute_key_length", "Attribute key length", base.DEC),
    attribute_key = ProtoField.bytes("graphx.attribute_key", "Attribute key"),
    attribute_value_length = ProtoField.uint32("graphx.attribute_value_length", "Attribute value length", base.DEC),
    attribute_value = ProtoField.bytes("graphx.attribute_value", "Attribute value"),
    payload_length = ProtoField.uint32("graphx.payload_length", "Payload length", base.DEC),
    payload = ProtoField.bytes("graphx.payload", "Payload"),
}

local malformed = ProtoExpert.new("graphx.malformed", "Malformed GraphX frame",
    expert.group.MALFORMED, expert.severity.ERROR)
local unsupported = ProtoExpert.new("graphx.unsupported_version", "Unsupported GraphX wire version",
    expert.group.PROTOCOL, expert.severity.WARN)
graphx.fields = fields
graphx.experts = {malformed, unsupported}

local MAX_ENVELOPE_BYTES = 16777216
local MAX_ATTRIBUTES = 4096

local function fail(item, message, unsupported_version)
    item:add_proto_expert_info(unsupported_version and unsupported or malformed, message)
    return false
end

local function require_bytes(item, offset, amount, limit, label)
    if amount < 0 or offset > limit or amount > limit - offset then
        fail(item, "truncated " .. label)
        return false
    end
    return true
end

local function add_byte_string(buffer, item, offset, limit, length_field, value_field, label)
    if not require_bytes(item, offset, 4, limit, label .. " length") then return nil end
    local length = buffer(offset, 4):uint()
    item:add(length_field, buffer(offset, 4))
    offset = offset + 4
    if not require_bytes(item, offset, length, limit, label) then return nil end
    if length > 0 then item:add(value_field, buffer(offset, length)) end
    return offset + length, length
end

local function all_zero(buffer, offset, length)
    for index = 0, length - 1 do
        if buffer(offset + index, 1):uint() ~= 0 then return false end
    end
    return true
end

function graphx.dissector(buffer, pinfo, tree)
    pinfo.cols.protocol = "GRAPHX"
    local packet_length = buffer:len()
    local root = tree:add(graphx, buffer(), "GraphX Framed Envelope")
    if packet_length < 4 then return fail(root, "truncated stream length prefix") end

    local envelope_length = buffer(0, 4):uint()
    root:add(fields.frame_length, buffer(0, 4))
    if envelope_length == 0 or envelope_length > MAX_ENVELOPE_BYTES then
        return fail(root, "envelope length is outside 1..16777216")
    end
    if envelope_length ~= packet_length - 4 then
        return fail(root, "length prefix does not match captured packet size")
    end
    local limit = packet_length
    if not require_bytes(root, 4, 20, limit, "envelope header") then return false end
    if buffer(4, 3):string() ~= "GXE" then return fail(root, "invalid GraphX magic") end
    root:add(fields.magic, buffer(4, 3))
    local version = buffer(7, 1):uint()
    root:add(fields.version, buffer(7, 1))
    if version ~= 1 and version ~= 2 then
        return fail(root, "unsupported envelope wire version " .. version, true)
    end
    root:add(fields.sequence, buffer(8, 8))
    root:add(fields.timestamp, buffer(16, 8))
    local offset = 24
    if version == 2 then
        if not require_bytes(root, offset, 48, limit, "version 2 identities") then return false end
        if all_zero(buffer, offset, 16) then
            return fail(root, "zero envelope message identity is invalid")
        end
        root:add(fields.message_id, buffer(offset, 16)); offset = offset + 16
        if all_zero(buffer, offset, 16) then
            return fail(root, "zero envelope trace identity is invalid")
        end
        root:add(fields.trace_id, buffer(offset, 16)); offset = offset + 16
        root:add(fields.parent_message_id, buffer(offset, 16)); offset = offset + 16
    end

    local type_length
    offset, type_length = add_byte_string(buffer, root, offset, limit,
        fields.type_length, fields.message_type, "type")
    if not offset then return false end
    local type_text = ""
    if type_length > 0 and type_length <= 64 then
        type_text = buffer(offset - type_length, type_length):string():gsub("[^%g ]", ".")
    end
    if version == 1 then
        offset = add_byte_string(buffer, root, offset, limit,
            fields.legacy_trace_length, fields.legacy_trace_id, "legacy trace ID")
        if not offset then return false end
    end
    if not require_bytes(root, offset, 4, limit, "attribute count") then return false end
    local attribute_count = buffer(offset, 4):uint()
    root:add(fields.attribute_count, buffer(offset, 4)); offset = offset + 4
    if attribute_count > MAX_ATTRIBUTES then return fail(root, "attribute count exceeds 4096") end
    local attribute_keys = {}
    for index = 1, attribute_count do
        local key_length
        offset, key_length = add_byte_string(buffer, root, offset, limit,
            fields.attribute_key_length, fields.attribute_key, "attribute " .. index .. " key")
        if not offset then return false end
        local key = buffer(offset - key_length, key_length):raw()
        if attribute_keys[key] then return fail(root, "duplicate envelope attribute key") end
        attribute_keys[key] = true
        offset = add_byte_string(buffer, root, offset, limit,
            fields.attribute_value_length, fields.attribute_value, "attribute " .. index .. " value")
        if not offset then return false end
    end
    offset = add_byte_string(buffer, root, offset, limit,
        fields.payload_length, fields.payload, "payload")
    if not offset then return false end
    if offset ~= limit then return fail(root, "trailing envelope bytes") end
    pinfo.cols.info = "v" .. version .. " seq=" .. tostring(buffer(8, 8):uint64()) ..
        (type_text ~= "" and " type=" .. type_text or "")
    return packet_length
end

local encapsulation = DissectorTable.get("wtap_encap")
assert(wtap.USER0, "this Wireshark build does not expose USER0 encapsulation")
-- USER0 already has Wireshark's generic user-DLT handler. Replace that default
-- for this explicitly loaded plugin so 4.2 and newer select GraphX reliably.
encapsulation:set(wtap.USER0, graphx)
