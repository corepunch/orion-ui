---
layout: default
title: Ledger executor operation contracts
nav_exclude: true
---

# Executor operation contracts

<!-- Generated from apps/ledger/executors.xml; do not edit. -->

Plans have optional `document = {field = value}` and `replace = {table = {rows...}}` sections. Only the fields and tables declared below are accepted. Parent references and new IDs are assigned by the host. All returned child fields are required. Unknown fields, invalid references/types, non-finite values and overlong strings fail atomically.

## `documents.number`

Preserve existing numbers; assign the next number across document types sharing the selected prefix.

- Document patch fields: `number`.
- Replace child table: `none`; fields: `none`.
- Recalculate after apply: `false`.

## `payroll.fill`

Replace payroll lines with positive salaries for accruals or outstanding employee balances for payouts.

- Document patch fields: `none`.
- Replace child table: `payroll_lines`; fields: `employee_id,amount`.
- Recalculate after apply: `true`.
