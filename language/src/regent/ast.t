-- Copyright 2015 Stanford University, NVIDIA Corporation
--
-- Licensed under the Apache License, Version 2.0 (the "License");
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     http://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

-- Legion AST

local ast_factory = {}

local function make_factory(name)
  return setmetatable(
    {
      parent = false,
      name = name,
      expected_fields = false,
      print_collapsed = false,
    },
    ast_factory)
end

local ast = make_factory("ast")
ast.make_factory = make_factory

-- Nodes

local ast_node = {}

function ast_node:__index(field)
  local value = ast_node[field]
  if value ~= nil then
    return value
  end
  local node_type = tostring(rawget(self, "node_type")) or "(unknown)"
  error(node_type .. " has no field '" .. field .. "' (in lookup)", 2)
end

function ast_node:__newindex(field, value)
  local node_type = tostring(rawget(self, "node_type")) or "(unknown)"
  error(node_type .. " has no field '" .. field .. "' (in assignment)", 2)
end

function ast.is_node(node)
  return type(node) == "table" and getmetatable(node) == ast_node
end

local function ast_node_tostring(node, indent)
  local newline = "\n"
  local spaces = string.rep("  ", indent)
  local spaces1 = string.rep("  ", indent + 1)
  if ast.is_node(node) then
    local collapsed = node.node_type.print_collapsed
    if collapsed then
      newline = ""
      spaces = ""
      spaces1 = ""
    end
    local str = tostring(node.node_type) .. "(" .. newline
    for k, v in pairs(node) do
      if k ~= "node_type" then
        str = str .. spaces1 .. k .. " = " ..
          ast_node_tostring(v, indent + 1) .. "," .. newline
      end
    end
    return str .. spaces .. ")"
  elseif terralib.islist(node) then
    local str = "{" .. newline
    for i, v in ipairs(node) do
      str = str .. spaces1 ..
        ast_node_tostring(v, indent + 1) .. "," .. newline
    end
    return str .. spaces .. "}"
  elseif type(node) == "string" then
    return string.format("%q", node)
  else
    return tostring(node)
  end
end

function ast_node:__tostring()
  return ast_node_tostring(self, 0)
end

function ast_node:printpretty()
  print(tostring(self))
end

function ast_node:is(node_type)
  return self.node_type:is(node_type)
end

function ast_node:type()
  return self.node_type
end

function ast_node:__call(fields_to_update)
  local ctor = rawget(self, "node_type")
  local values = {}
  for _, f in ipairs(ctor.expected_fields) do
    values[f] = self[f]
  end
  for f, v in pairs(fields_to_update) do
    if values[f] == nil then
      error(tostring(ctor) .. " does not require argument '" .. f .. "'", 2)
    end
    values[f] = v
  end
  return ctor(values)
end

-- Constructors

local ast_ctor = {}

function ast_ctor:__index(field)
  local value = ast_ctor[field]
  if value ~= nil then
    return value
  end
  error(tostring(self) .. " has no field '" .. field .. "'", 2)
end

function ast_ctor:__call(node)
  assert(type(node) == "table", tostring(self) .. " expected table")
  for i, f in ipairs(self.expected_fields) do
    if rawget(node, f) == nil then
      error(tostring(self) .. " missing required argument '" .. f .. "'", 2)
    end
  end
  rawset(node, "node_type", self)
  setmetatable(node, ast_node)
  return node
end

function ast_ctor:__tostring()
  return tostring(self.parent) .. "." .. self.name
end

function ast_ctor:is(node_type)
  return self == node_type or self.parent:is(node_type)
end

-- Factories

local function merge_fields(...)
  local keys = {}
  local result = terralib.newlist({})
  for _, fields in ipairs({...}) do
    if fields then
      for _, field in ipairs(fields) do
        if keys[field] then
          error("multiple definitions of field " .. field)
        end
        keys[field] = true
        result:insert(field)
      end
    end
  end
  return result
end

function ast_factory:__index(field)
  local value = ast_factory[field]
  if value ~= nil then
    return value
  end
  error(tostring(self) .. " has no field '" .. field .. "'", 2)
end

function ast_factory:inner(ctor_name, expected_fields, print_collapsed)
  local ctor = setmetatable(
    {
      parent = self,
      name = ctor_name,
      expected_fields = merge_fields(self.expected_fields, expected_fields),
      print_collapsed = (print_collapsed == nil and self.print_collapsed) or print_collapsed or false
    }, ast_factory)

  assert(rawget(self, ctor_name) == nil,
         "multiple definitions of constructor " .. ctor_name)
  self[ctor_name] = ctor
  return ctor
end

function ast_factory:leaf(ctor_name, expected_fields, print_collapsed)
  local ctor = setmetatable(
    {
      parent = self,
      name = ctor_name,
      expected_fields = merge_fields(self.expected_fields, expected_fields),
      print_collapsed = (print_collapsed == nil and self.print_collapsed) or print_collapsed or false
    }, ast_ctor)

  assert(rawget(self, ctor_name) == nil,
         "multiple definitions of constructor " .. ctor_name)
  self[ctor_name] = ctor
  return ctor
end

function ast_factory:is(node_type)
  return self == node_type or (self.parent and self.parent:is(node_type))
end

function ast_factory:__tostring()
  if self.parent then
    return tostring(self.parent) .. "." .. self.name
  end
  return self.name
end

-- Traversal

function ast.traverse_node_postorder(fn, node)
  if ast.is_node(node) then
    for _, child in pairs(node) do
      ast.traverse_node_postorder(fn, child)
    end
    fn(node)
  elseif terralib.islist(node) then
    for _, child in ipairs(node) do
      ast.traverse_node_postorder(fn, child)
    end
  end
end

function ast.map_node_postorder(fn, node)
  if ast.is_node(node) then
    local tmp = {}
    for k, child in pairs(node) do
      if k ~= "node_type" then
        tmp[k] = ast.map_node_postorder(fn, child)
      end
    end
    return fn(node(tmp))
  elseif terralib.islist(node) then
    local tmp = terralib.newlist()
    for _, child in ipairs(node) do
      tmp:insert(ast.map_node_postorder(fn, child))
    end
    return tmp
  end
  return node
end

function ast.traverse_expr_postorder(fn, node)
  ast.traverse_node_postorder(
    function(child)
      if rawget(child, "expr_type") then
        fn(child)
      end
    end,
    node)
end

-- Location

ast:inner("location")
ast.location:leaf("Position", {"line", "offset"}, true)
ast.location:leaf("Span", {"source", "start", "stop"})

-- Helpers for extracting location from token stream.
local function position_from_start(token)
  return ast.location.Position {
    line = token.linenumber,
    offset = token.offset
  }
end

local function position_from_stop(token)
  return position_from_start(token)
end

function ast.save(p)
  return position_from_start(p:cur())
end

function ast.span(start, p)
  return ast.location.Span {
    source = p.source,
    start = start,
    stop = position_from_stop(p:cur()),
  }
end

function ast.empty_span(p)
  return ast.location.Span {
    source = p.source,
    start = ast.location.Position { line = 0, offset = 0 },
    stop = ast.location.Position { line = 0, offset = 0 },
  }
end

function ast.trivial_span()
  return ast.location.Span {
    source = "",
    start = ast.location.Position { line = 0, offset = 0 },
    stop = ast.location.Position { line = 0, offset = 0 },
  }
end

-- Options

ast:inner("options")

-- Options: Dispositions
ast.options:leaf("Allow", {"value"}, true)
ast.options:leaf("Demand", {"value"}, true)
ast.options:leaf("Forbid", {"value"}, true)

-- Options: Values
ast.options:leaf("Unroll", {"value"}, true)

-- Options: Sets
ast.options:leaf("Set", {"cuda", "inline", "parallel", "spmd", "vectorize"})

function ast.default_options()
  local allow = ast.options.Allow { value = false }
  return ast.options.Set {
    cuda = allow,
    inline = allow,
    parallel = allow,
    spmd = allow,
    vectorize = allow,
  }
end

-- Node Types (Unspecialized)

ast:inner("unspecialized", {"span"})

ast.unspecialized:inner("expr", {"options"})
ast.unspecialized.expr:leaf("ID", {"name"})
ast.unspecialized.expr:leaf("Escape", {"expr"})
ast.unspecialized.expr:leaf("FieldAccess", {"value", "field_names"})
ast.unspecialized.expr:leaf("IndexAccess", {"value", "index"})
ast.unspecialized.expr:leaf("MethodCall", {"value", "method_name", "args"})
ast.unspecialized.expr:leaf("Call", {"fn", "args"})
ast.unspecialized.expr:leaf("Ctor", {"fields"})
ast.unspecialized.expr:leaf("CtorListField", {"value"})
ast.unspecialized.expr:leaf("CtorRecField", {"name_expr", "value"})
ast.unspecialized.expr:leaf("Constant", {"value", "expr_type"})
ast.unspecialized.expr:leaf("RawContext")
ast.unspecialized.expr:leaf("RawFields", {"region"})
ast.unspecialized.expr:leaf("RawPhysical", {"region"})
ast.unspecialized.expr:leaf("RawRuntime")
ast.unspecialized.expr:leaf("RawValue", {"value"})
ast.unspecialized.expr:leaf("Isnull", {"pointer"})
ast.unspecialized.expr:leaf("New", {"pointer_type_expr"})
ast.unspecialized.expr:leaf("Null", {"pointer_type_expr"})
ast.unspecialized.expr:leaf("DynamicCast", {"type_expr", "value"})
ast.unspecialized.expr:leaf("StaticCast", {"type_expr", "value"})
ast.unspecialized.expr:leaf("Ispace", {"index_type_expr", "extent", "start"})
ast.unspecialized.expr:leaf("Region", {"ispace", "fspace_type_expr"})
ast.unspecialized.expr:leaf("Partition", {"disjointness_expr",
                                          "region_type_expr", "coloring"})
ast.unspecialized.expr:leaf("CrossProduct", {"arg_type_exprs"})
ast.unspecialized.expr:leaf("Unary", {"op", "rhs"})
ast.unspecialized.expr:leaf("Binary", {"op", "lhs", "rhs"})
ast.unspecialized.expr:leaf("Deref", {"value"})

ast.unspecialized:leaf("Block", {"stats"})

ast.unspecialized:inner("stat", {"options"})
ast.unspecialized.stat:leaf("If", {"cond", "then_block", "elseif_blocks",
                                   "else_block"})
ast.unspecialized.stat:leaf("Elseif", {"cond", "block"})
ast.unspecialized.stat:leaf("While", {"cond", "block"})
ast.unspecialized.stat:leaf("ForNum", {"name", "type_expr", "values", "block"})
ast.unspecialized.stat:leaf("ForList", {"name", "type_expr", "value", "block"})
ast.unspecialized.stat:leaf("Repeat", {"block", "until_cond"})
ast.unspecialized.stat:leaf("Block", {"block"})
ast.unspecialized.stat:leaf("Var", {"var_names", "type_exprs", "values"})
ast.unspecialized.stat:leaf("VarUnpack", {"var_names", "fields", "value"})
ast.unspecialized.stat:leaf("Return", {"value"})
ast.unspecialized.stat:leaf("Break")
ast.unspecialized.stat:leaf("Assignment", {"lhs", "rhs"})
ast.unspecialized.stat:leaf("Reduce", {"op", "lhs", "rhs"})
ast.unspecialized.stat:leaf("Expr", {"expr"})

ast.unspecialized:leaf("Constraint", {"lhs", "op", "rhs"})
ast.unspecialized:leaf("Privilege", {"privilege", "op", "regions"})
ast.unspecialized:leaf("PrivilegeRegion", {"region_name", "fields"})
ast.unspecialized:leaf("PrivilegeRegionField", {"field_name", "fields"})

ast.unspecialized.stat:leaf("Task", {"name", "params", "return_type_expr",
                               "privileges", "constraints", "body"})
ast.unspecialized.stat:leaf("TaskParam", {"param_name", "type_expr"})
ast.unspecialized.stat:leaf("Fspace", {"name", "params", "fields",
                                       "constraints"})
ast.unspecialized.stat:leaf("FspaceParam", {"param_name", "type_expr"})
ast.unspecialized.stat:leaf("FspaceField", {"field_name", "type_expr"})

-- Node Types (Specialized)

ast:inner("specialized", {"span"})

ast.specialized:inner("expr", {"options"})
ast.specialized.expr:leaf("ID", {"value"})
ast.specialized.expr:leaf("FieldAccess", {"value", "field_name"})
ast.specialized.expr:leaf("IndexAccess", {"value", "index"})
ast.specialized.expr:leaf("MethodCall", {"value", "method_name", "args"})
ast.specialized.expr:leaf("Call", {"fn", "args"})
ast.specialized.expr:leaf("Cast", {"fn", "args"})
ast.specialized.expr:leaf("Ctor", {"fields", "named"})
ast.specialized.expr:leaf("CtorListField", {"value"})
ast.specialized.expr:leaf("CtorRecField", {"name", "value"})
ast.specialized.expr:leaf("Constant", {"value", "expr_type"})
ast.specialized.expr:leaf("RawContext")
ast.specialized.expr:leaf("RawFields", {"region"})
ast.specialized.expr:leaf("RawPhysical", {"region"})
ast.specialized.expr:leaf("RawRuntime")
ast.specialized.expr:leaf("RawValue", {"value"})
ast.specialized.expr:leaf("Isnull", {"pointer"})
ast.specialized.expr:leaf("New", {"pointer_type", "region"})
ast.specialized.expr:leaf("Null", {"pointer_type"})
ast.specialized.expr:leaf("DynamicCast", {"value", "expr_type"})
ast.specialized.expr:leaf("StaticCast", {"value", "expr_type"})
ast.specialized.expr:leaf("Ispace", {"index_type", "extent", "start",
                                     "expr_type"})
ast.specialized.expr:leaf("Region", {"ispace", "ispace_symbol", "fspace_type",
                                     "expr_type"})
ast.specialized.expr:leaf("Partition", {"disjointness", "region", "coloring",
                                        "expr_type"})
ast.specialized.expr:leaf("CrossProduct", {"args", "expr_type"})
ast.specialized.expr:leaf("Function", {"value"})
ast.specialized.expr:leaf("Unary", {"op", "rhs"})
ast.specialized.expr:leaf("Binary", {"op", "lhs", "rhs"})
ast.specialized.expr:leaf("Deref", {"value"})
ast.specialized.expr:leaf("LuaTable", {"value"})

ast.specialized:leaf("Block", {"stats"})

ast.specialized:inner("stat", {"options"})
ast.specialized.stat:leaf("If", {"cond", "then_block", "elseif_blocks",
                                 "else_block"})
ast.specialized.stat:leaf("Elseif", {"cond", "block"})
ast.specialized.stat:leaf("While", {"cond", "block"})
ast.specialized.stat:leaf("ForNum", {"symbol", "values", "block"})
ast.specialized.stat:leaf("ForList", {"symbol", "value", "block"})
ast.specialized.stat:leaf("Repeat", {"block", "until_cond"})
ast.specialized.stat:leaf("Block", {"block"})
ast.specialized.stat:leaf("Var", {"symbols", "values"})
ast.specialized.stat:leaf("VarUnpack", {"symbols", "fields", "value"})
ast.specialized.stat:leaf("Return", {"value"})
ast.specialized.stat:leaf("Break")
ast.specialized.stat:leaf("Assignment", {"lhs", "rhs"})
ast.specialized.stat:leaf("Reduce", {"op", "lhs", "rhs"})
ast.specialized.stat:leaf("Expr", {"expr"})

ast.specialized.stat:leaf("Task", {"name", "params", "return_type", "privileges",
                                   "constraints", "body", "prototype"})
ast.specialized.stat:leaf("TaskParam", {"symbol"})
ast.specialized.stat:leaf("Fspace", {"name", "fspace"})

-- Node Types (Typed)

ast.typed = ast:inner("typed", {"span"})

ast.typed:inner("expr", {"options", "expr_type"})
ast.typed.expr:leaf("Internal", {"value"}) -- internal use only

ast.typed.expr:leaf("ID", {"value"})
ast.typed.expr:leaf("FieldAccess", {"value", "field_name"})
ast.typed.expr:leaf("IndexAccess", {"value", "index"})
ast.typed.expr:leaf("MethodCall", {"value", "method_name", "args"})
ast.typed.expr:leaf("Call", {"fn", "args"})
ast.typed.expr:leaf("Cast", {"fn", "arg"})
ast.typed.expr:leaf("Ctor", {"fields", "named"})
ast.typed.expr:leaf("CtorListField", {"value"})
ast.typed.expr:leaf("CtorRecField", {"name", "value"})
ast.typed.expr:leaf("RawContext")
ast.typed.expr:leaf("RawFields", {"region", "fields"})
ast.typed.expr:leaf("RawPhysical", {"region", "fields"})
ast.typed.expr:leaf("RawRuntime")
ast.typed.expr:leaf("RawValue", {"value"})
ast.typed.expr:leaf("Isnull", {"pointer"})
ast.typed.expr:leaf("New", {"pointer_type", "region"})
ast.typed.expr:leaf("Null", {"pointer_type"})
ast.typed.expr:leaf("DynamicCast", {"value"})
ast.typed.expr:leaf("StaticCast", {"value", "parent_region_map"})
ast.typed.expr:leaf("Ispace", {"index_type", "extent", "start"})
ast.typed.expr:leaf("Region", {"ispace", "fspace_type"})
ast.typed.expr:leaf("Partition", {"disjointness", "region", "coloring"})
ast.typed.expr:leaf("CrossProduct", {"args"})
ast.typed.expr:leaf("Constant", {"value"})
ast.typed.expr:leaf("Function", {"value"})
ast.typed.expr:leaf("Unary", {"op", "rhs"})
ast.typed.expr:leaf("Binary", {"op", "lhs", "rhs"})
ast.typed.expr:leaf("Deref", {"value"})
ast.typed.expr:leaf("Future", {"value"})
ast.typed.expr:leaf("FutureGetResult", {"value"})

ast.typed:leaf("Block", {"stats"})

ast.typed:inner("stat", {"options"})
ast.typed.stat:leaf("If", {"cond", "then_block", "elseif_blocks", "else_block"})
ast.typed.stat:leaf("Elseif", {"cond", "block"})
ast.typed.stat:leaf("While", {"cond", "block"})
ast.typed.stat:leaf("ForNum", {"symbol", "values", "block"})
ast.typed.stat:leaf("ForList", {"symbol", "value", "block"})
ast.typed.stat:leaf("ForListVectorized", {"symbol", "value", "block",
                                          "orig_block", "vector_width"})
ast.typed.stat:leaf("Repeat", {"block", "until_cond"})
ast.typed.stat:leaf("Block", {"block"})
ast.typed.stat:leaf("IndexLaunch", {"symbol", "domain", "call", "reduce_lhs",
                                    "reduce_op", "args_provably"})
ast:leaf("IndexLaunchArgsProvably", {"invariant", "variant"})
ast.typed.stat:leaf("Var", {"symbols", "types", "values"})
ast.typed.stat:leaf("VarUnpack", {"symbols", "fields", "field_types", "value"})
ast.typed.stat:leaf("Return", {"value"})
ast.typed.stat:leaf("Break")
ast.typed.stat:leaf("Assignment", {"lhs", "rhs"})
ast.typed.stat:leaf("Reduce", {"op", "lhs", "rhs"})
ast.typed.stat:leaf("Expr", {"expr"})
ast.typed.stat:leaf("MapRegions", {"region_types"})
ast.typed.stat:leaf("UnmapRegions", {"region_types"})

ast:leaf("TaskConfigOptions", {"leaf", "inner", "idempotent"})

ast.typed.stat:leaf("Task", {"name", "params", "return_type", "privileges",
                             "constraints", "body", "config_options",
                             "region_divergence", "prototype"})
ast.typed.stat:leaf("TaskParam", {"symbol", "param_type"})
ast.typed.stat:leaf("Fspace", {"name", "fspace"})

return ast
