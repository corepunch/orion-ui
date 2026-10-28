#!/usr/bin/env python3
"""Validate an emitted UBL invoice using official OASIS XSD and CEN rules.

Install developer dependencies: python3 -m pip install lxml saxonche
See docs/invoice-forms-audit.md for official artefacts and the pinned release.
This is a release/QA tool, not a claim of XRechnung or filing certification.
"""
import argparse
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('invoice', type=Path)
    parser.add_argument('--xsd', required=True, type=Path, help='OASIS UBL-Invoice-2.1.xsd')
    parser.add_argument('--rules', required=True, type=Path, help='CEN EN16931-UBL-validation.xslt')
    parser.add_argument('--report', type=Path, help='Optional full SVRL output')
    args = parser.parse_args()
    try:
        from lxml import etree
        from saxonche import PySaxonProcessor
    except ImportError as error:
        parser.error(f'{error}; install the documented developer dependencies')
    try:
        xml_parser = etree.XMLParser(resolve_entities=False, no_network=True)
        schema = etree.XMLSchema(etree.parse(str(args.xsd), xml_parser))
        schema.assertValid(etree.parse(str(args.invoice), xml_parser))
        with PySaxonProcessor(license=False) as processor:
            compiled = processor.new_xslt30_processor().compile_stylesheet(stylesheet_file=str(args.rules.resolve()))
            report = compiled.transform_to_string(source_file=str(args.invoice.resolve()))
        result = etree.fromstring(report.encode(), xml_parser)
        failures = result.xpath('//*[local-name()="failed-assert"]')
        if args.report:
            args.report.write_text(report, encoding='utf-8')
        for failure in failures:
            print(f'{failure.get("id")}: {" ".join("".join(failure.itertext()).split())}', file=sys.stderr)
        if failures:
            return 1
    except Exception as error:
        print(f'Invoice validation failed: {error}', file=sys.stderr)
        return 1
    print(f'{args.invoice}: OASIS UBL 2.1 XSD valid; no CEN EN16931 failed assertions')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
