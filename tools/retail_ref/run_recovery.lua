-- Replays a script against the retail recovery.lua, unmodified.
-- Usage: luajit run_recovery.lua <retail file> <script file>
local M = dofile(arg[1])
local ops = dofile(arg[2])
local r = M.new()
local function proof(p)
  if not p then return nil end
  return {fresh = p[1], safe = p[2], same_player = p[3], foreign = p[4]}
end
local function state(s)
  if not s then return nil end
  return {failure_kind = s[1] and 'temporary_read' or 'other', failure_native_error = s[2]}
end
local function row(x)
  if not x then return '-' end
  return string.format('%s %d %s %s %.17g %.17g', x.phase, x.attempts, x.classification or '-',
    x.pending_classification or '-', x.next_retry or -1, x.proof_deadline or -1)
end
for _, op in ipairs(ops) do
  local kind = op[1]
  if kind == 'classify' then print(M.classify(op.name, state(op.state), op.clean, proof(op.proof)))
  elseif kind == 'failure' then print(row(r:failure(op.name, state(op.state), op.clean, proof(op.proof), op.now, 'x')))
  elseif kind == 'cleanup' then print(tostring(r:cleanup(op.name, op.done, op.now)))
  elseif kind == 'action' then print(r:action(op.name, op.now))
  elseif kind == 'needs_proof' then print(tostring(r:needs_proof(op.name)))
  elseif kind == 'has_policies' then print(tostring(r:has_policies()))
  elseif kind == 'revalidate' then print(tostring(r:revalidate(op.name, proof(op.proof), op.now)))
  elseif kind == 'success' then r:success(op.name, op.now, op.active); print(row(r:snapshot().policies[op.name]))
  elseif kind == 'blocks_view' then local b, n, w = r:blocks_view(op.now); print(tostring(b) .. ' ' .. (n or '-') .. ' ' .. (w or '-'))
  elseif kind == 'reset' then r:reset(); print('ok')
  elseif kind == 'owner_reset' then r:owner_reset(); print('ok')
  elseif kind == 'row' then print(row(r:snapshot().policies[op.name]))
  end
end
