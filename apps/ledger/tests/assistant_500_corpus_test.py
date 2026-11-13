#!/usr/bin/env python3
"""Run every request in the 500-row Markdown corpus through Ledger's real Lua parser/engine.

From the repository root:
  python3 apps/ledger/tests/assistant_500_corpus_test.py
  python3 apps/ledger/tests/assistant_500_corpus_test.py --strict

The mock host contains synthetic records only and makes open() a read-only log.
This is an architectural recognition probe, not a UI, datasource, accounting, or
production-infobase test. Topic detection is exercised through the real topic
matcher; rendering/accounting topic handlers need the native Ledger host.
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
CORPUS = ROOT / "apps/ledger/docs/assistant-500-accountant-requests.md"

LUA_RUNNER = r'''package.path='apps/ledger/share/addons/assistant/?.lua;build/share/ledger/addons/assistant/?.lua;share/lua/?.lua;share/lua/?/init.lua;'..package.path
local P,E,T=require('parser'),require('engine'),require('topics')
local corpus=assert(io.open(arg[1],'r'))
local strict=arg[2]=='--strict'
local records={
  counterparties={
    {id=1,name='Альфа',tax_id='TEST-1',country_code='RU',debt=1200,owed=0},
    {id=2,name='Бета',tax_id='TEST-2',country_code='RU',debt=0,owed=2400},
    {id=3,name='Альфа',tax_id='TEST-1',country_code='RU',debt=0,owed=0}},
  documents={
    {id=11,name='Поступление-1',number='П-1',date='2026-01-10',amount=1200,posted=true,counterparty_id=1},
    {id=12,name='Реализация-1',number='Р-1',date='2026-02-10',amount=2400,posted=false,counterparty_id=2}},
  items={
    {id=21,name='Бумага',sku='SKU-1',amount=50},
    {id=22,name='Карандаш',sku='SKU-2',amount=20}},
}
local function clone(t)
  if type(t)~='table' then return t end
  local out={}; for k,v in pairs(t) do out[k]=clone(v) end; return out
end
local function same(a,b)
  if type(a)~=type(b) then return false end
  if type(a)~='table' then return a==b end
  for k,v in pairs(a) do if not same(v,b[k]) then return false end end
  for k in pairs(b) do if a[k]==nil then return false end end
  return true
end
local counts,examples={},{}
local total=0
local focused={
  {text='покажи клиентов',expected='command'},
  {text='найди клиента "Альфа"',expected='command'},
  {text='покажи документы дата после 2026-01-01',expected='command'},
  {text='покажи должников',expected='command'},
  {text='найди дубли контрагентов',expected='command'},
  {text='проверь учет',expected='topic:checks'},
  {text='Мне нужно проверить контрагента по ИНН.',expected='no-topic-answer'},
  {text='Есть ли в базе номенклатуру с указанной ставкой НДС?',expected='no-topic-answer'},
  {text='Мне нужно проверить товары с отрицательным остатком.',expected='no-topic-answer'},
  {text='Есть ли в базе поставщиков с выданным авансом?',expected='no-topic-answer'},
}
local function expected_topic(n)
  local cat=math.floor((n-1)/50)+1
  local intent=math.floor(((n-1)%50)/5)
  if cat==3 then
    if intent==0 then return 'receivables' end
    if intent==1 then return 'payables' end
    return nil
  elseif cat==4 then
    return ({[0]='money',[1]='money',[2]='money',[3]='taxes',[4]='taxes',[5]='negative',[6]='negative',[7]='closing',[8]='closing',[9]='checks'})[intent]
  elseif cat==5 and intent==2 then return 'negative' end
  return nil
end
local function note(kind,id,request)
  counts[kind]=(counts[kind] or 0)+1
  examples[kind]=examples[kind] or {}
  if #examples[kind]<12 then examples[kind][#examples[kind]+1]=id..': '..request end
end
for line in corpus:lines() do
  local id,request,assistant,data,reason=line:match('^|%s*(%d+)%s*|%s*(.-)%s*|%s*(.-)%s*|%s*(.-)%s*|%s*(.-)%s*|%s*$')
  if id then
    total=total+1
    local before=clone(records)
    local opened={}
    local host={rows=function(entity) return assert(records[entity],'unknown mock entity '..tostring(entity)) end,
      open=function(entity,record_id) opened[#opened+1]={entity=entity,id=record_id}; return true end}
    local engine=E.new(host)
    local ok,ast,parse_error=pcall(P.parse,request,{})
    local result,topic,rest,topic_error
    local positive=assistant:find('Кандидат',1,true)~=nil or assistant:find('Есть ',1,true)==1 or assistant:find('Встроено',1,true)==1
    if not ok then
      note('parser_error',id,request..' :: '..tostring(ast))
    elseif ast then
      local ran,r=pcall(engine.submit,engine,request)
      if not ran then note('engine_error',id,request..' :: '..tostring(r))
      elseif type(r)~='table' or type(r.ok)~='boolean' then note('invalid_engine_result',id,request)
      elseif not same(records,before) then note('mock_write_detected',id,request)
      else
        result=r
        if positive then note('typed_command',id,request)
        else note('unintended_topic_fallback',id,request..' :: '..assistant) end
        for _,row in ipairs(r.rows or {}) do
          assert(type(row.id)=='number','engine returned a row without numeric id')
        end
      end
    elseif P.has_verb(request) then
      -- The UI only falls back to topic inference when parsing failed and no
      -- command verb was present. A command-shaped phrase must not bypass it.
      if positive then note('coverage_gap',id,request..' :: '..assistant)
      else note('expected_limitation',id,request) end
    else
      local tok,top,remaining,err=pcall(T.match,request)
      if not tok then note('topic_matcher_error',id,request..' :: '..tostring(top))
      else
        topic,rest,topic_error=top,remaining,err
        if topic and topic_error then
          note('topic_scope_rejection',id,request..' :: '..topic_error)
        elseif topic then
          local intended=expected_topic(tonumber(id))
          if intended==topic then note('fixed_topic',id,request)
          elseif positive then note('wrong_topic_fallback',id,request..' :: routed to '..topic..'; intended '..tostring(intended))
          else note('unintended_topic_fallback',id,request..' :: topic '..topic) end
        elseif positive then
          note('coverage_gap',id,request..' :: '..assistant)
        else
          note('expected_limitation',id,request)
        end
      end
    end
  end
end
corpus:close()
-- Focused controls make the broad corpus diagnostic, while keeping stable
-- executable examples and known false-topic hazards visible as regressions.
for _,case in ipairs(focused) do
  local ast,parse_error=P.parse(case.text,{})
  local topic,rest,topic_error=T.match(case.text)
  if case.expected=='command' then
    assert(ast,'focused command stopped parsing: '..case.text..' / '..tostring(parse_error))
    local e=E.new{rows=function(entity) return records[entity] end,open=function() return true end}
    local r=e:submit(case.text)
    assert(r.ok,'focused command failed in mock engine: '..case.text..' / '..tostring(r.message))
    note('focused_control_pass',0,case.text)
  elseif case.expected=='topic:checks' then
    assert(not ast and topic=='checks' and not topic_error,'focused topic did not match: '..case.text)
    note('focused_control_pass',0,case.text)
  else
    local actual=(not ast and not P.has_verb(case.text) and topic and not topic_error) and ('topic:'..topic) or 'no-topic-answer'
    if actual=='no-topic-answer' then note('focused_regression_pass',0,case.text)
    else note('focused_regression_failure',0,case.text..' / got '..actual) end
  end
end
assert(total==500,'expected 500 numbered corpus rows, got '..total)
for _,k in ipairs({'typed_command','fixed_topic','topic_scope_rejection','wrong_topic_fallback','expected_limitation','coverage_gap','unintended_topic_fallback','parser_error','engine_error','invalid_engine_result','mock_write_detected','topic_matcher_error','focused_control_pass','focused_regression_pass','focused_regression_failure'}) do
  if counts[k] then
    io.write(string.format('%-24s %d\n',k,counts[k]))
    for _,sample in ipairs(examples[k]) do io.write('  '..sample..'\n') end
  end
end
io.write(string.format('total rows checked       %d\n',total))
if not strict then
  io.write('REPORT ONLY: counts describe the current code and are not a pass claim. Re-run with --strict to fail on unresolved corpus gaps/fallbacks.\n')
end
if (counts.parser_error or 0)>0 or (counts.engine_error or 0)>0 or (counts.invalid_engine_result or 0)>0 or
   (counts.mock_write_detected or 0)>0 or (counts.topic_matcher_error or 0)>0 then os.exit(1) end
if strict and ((counts.coverage_gap or 0)>0 or (counts.wrong_topic_fallback or 0)>0 or
   (counts.unintended_topic_fallback or 0)>0 or (counts.focused_regression_failure or 0)>0) then os.exit(2) end
'''


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--strict", action="store_true", help="fail if a row marked as an assistant candidate is not recognized")
    args = parser.parse_args()
    if not CORPUS.is_file():
        print(f"Missing corpus: {CORPUS}", file=sys.stderr)
        return 2
    lua = shutil.which("lua") or shutil.which("lua5.4") or shutil.which("lua5.3")
    if not lua:
        print("Lua 5.3+ is required; install/use the repository's Lua runtime.", file=sys.stderr)
        return 2
    with tempfile.NamedTemporaryFile("w", suffix=".lua", encoding="utf-8", delete=False) as runner:
        runner.write(LUA_RUNNER)
        runner_path = Path(runner.name)
    try:
        proc = subprocess.run([lua, str(runner_path), str(CORPUS), "--strict" if args.strict else ""],
                              cwd=ROOT, text=True, capture_output=True, check=False)
    finally:
        runner_path.unlink(missing_ok=True)
    sys.stdout.write(proc.stdout)
    sys.stderr.write(proc.stderr)
    return proc.returncode


if __name__ == "__main__":
    raise SystemExit(main())
