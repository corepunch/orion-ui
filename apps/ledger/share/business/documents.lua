return function(ctx)
  local page = ctx.page
  local ledger, native = ctx.ledger, ctx.native
  return {
    id="ledger.documents", api=1, dependencies={"ledger.counterparties", "ledger.items"},
    owns={"ib.documents", "ib.doc_lines", "ib.posting_revisions", "ib.revision_lines", "ib.revision_entries", "ib.reversal_links"},
    commands={
      {id="documents.list", mode="read", roles=ctx.view, run=function(args)
        local rows = ledger.fetch("ib", "documents")
        return page(rows, args)
      end},
      {id="documents.lines", mode="read", roles=ctx.view, run=function(args)
        assert(type(args.document_id) == "number" and math.type(args.document_id) == "integer" and args.document_id > 0, "invalid document id")
        local exact, rows = false, {}
        for _, doc in ipairs(ledger.fetch("ib", "documents")) do
          if doc.id == args.document_id then exact = doc.exact_money; break end
        end
        for _, row in ipairs(ledger.fetch("ib", "doc_lines")) do
          if row.document_id == args.document_id then row.exact_money = exact; rows[#rows + 1] = row end
        end
        return page(rows, args)
      end},
      {id="documents.history", mode="read", roles=ctx.view, run=function(args)
        assert(type(args.document_id) == "number" and math.type(args.document_id) == "integer" and args.document_id > 0, "invalid document id")
        local rows = {}
        for _, row in ipairs(ledger.fetch("ib", "posting_revisions")) do
          if row.document_id == args.document_id then rows[#rows + 1] = row end
        end
        return page(rows, args)
      end},
      {id="documents.history_entries", mode="read", roles=ctx.view, run=function(args)
        assert(type(args.revision_id) == "number" and math.type(args.revision_id) == "integer" and args.revision_id > 0, "invalid revision id")
        local rows = {}
        for _, row in ipairs(ledger.fetch("ib", "revision_entries")) do
          if row.revision_id == args.revision_id then rows[#rows + 1] = row end
        end
        return page(rows, args)
      end},
      {id="documents.history_lines", mode="read", roles=ctx.view, run=function(args)
        assert(type(args.revision_id) == "number" and math.type(args.revision_id) == "integer" and args.revision_id > 0, "invalid revision id")
        local revision
        for _, row in ipairs(ledger.fetch("ib", "posting_revisions")) do if row.id == args.revision_id then revision = row; break end end
        assert(revision, "unknown revision")
        local rows = {}
        if revision.lines_captured then
          for _, row in ipairs(ledger.fetch("ib", "revision_lines")) do if row.revision_id == revision.id then rows[#rows + 1] = row end end
        end
        return {available = revision.lines_captured, lines = page(rows, args)}
      end},
      {id="documents.create", mode="write", roles=ctx.edit, run=function(args)
        return {document_id = native.create(args)}
      end},
      {id="documents.delete", mode="write", roles=ctx.edit, run=function(args)
        assert(type(args.document_id) == "number" and math.type(args.document_id) == "integer", "invalid document id")
        return {document_id = native.delete(args.document_id)}
      end},
      {id="documents.post", mode="write", roles=ctx.edit, run=function(args)
        assert(type(args.document_id) == "number" and math.type(args.document_id) == "integer", "invalid document id")
        return {document_id = native.post(args.document_id)}
      end},
      {id="documents.update", mode="write", roles=ctx.edit, run=function(args)
        return {document_id = native.update(args)}
      end},
      {id="documents.replace_lines", mode="write", roles=ctx.edit, run=function(args)
        return {document_id = native.replace_lines(args)}
      end},
      {id="documents.reverse", mode="write", roles=ctx.edit, run=function(args)
        assert(type(args.document_id) == "number" and math.type(args.document_id) == "integer", "invalid document id")
        assert(type(args.date) == "string" and type(args.reason) == "string", "invalid reversal details")
        return {document_id = native.reverse(args.document_id, args.date, args.reason)}
      end},
    },
  }
end
