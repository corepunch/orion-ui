# 1CD format notes (read-only importer reference)

Scope: gaps beyond `raw1cd.py`, `onecd_metadata.py`, `1cd-verification-without-1c.md`.
Tags: **[V]** verified on `DemoAccountingEduc-1Cv8.1CD` (format 8.3.8.0, page 8192, 135671 pages, 6092 tables, IBVERSION 6 / platform req 80312); **[S: repo/file]** read from source code; **[A]** from article, unverified; **[NR]** not researched.
Source keys: T1CD = https://github.com/e8tools/Tool1CD (src/tool1cd), DTOOLS = https://github.com/Infactum/onec_dtools, DAJET = https://github.com/zhichkin/dajet-metadata, AWA = https://infostart.ru/blogs/1033/, AWA838 = https://infostart.ru/1c/articles/536343/, ITS-FIELDS = https://its.1c.ru/db/content/metod8dev/src/admins/i8101798.htm.

## 1. File format versions

Header (page 0), little endian: `"1CDBMSV8"`(8) · ver1..ver4 (4 bytes, e.g. 8,3,8,0) · u32 total pages · i32 unknown (always 1 in old files) · **8.3.8 only:** u32 page size at offset 20. [S: T1CD Class_1CD.h `v8con`; AWA838] [V for 8.3.8]

| Header ver | Page size | Notes |
|---|---|---|
| 8.0.3.0, 8.0.5.0 | 4096 | 1Cv8.0 bases. Root object `lang[8]`, i32 numblocks, table-descr object numbers. T1CD read-only. [S: T1CD Class_1CD.cpp; AWA] |
| 8.1.0.0, 8.2.0.0 | 4096 | 1Cv8.1/8.2 bases. Root `lang[32]` + numblocks + blocks. [S; AWA] |
| 8.2.14.0 | 4096 | Platform 8.2.14 up to 8.3.7 write it. [S: onec_dtools accepts only 8.2.14.0 and 8.3.8.0; 1c-dn page "transition to 8.2.14"] |
| 8.3.8.0 | 4096/8192/16384/32768/65536 | Platforms 8.3.8+ (also 8.3.x, 8.5). Header version need not equal creating platform. [S: T1CD README] |

Recognise: bytes 0..7 signature, 8..11 version, page size at 20 (8.3.8) else 4096. Checks: file size = pages * page size and header page count equal [S: T1CD]. 8.3.8+ platforms open any format; <=8.3.7 open 8.2.14 only. Conversion: `cnvdbfl -i <file>` shows format/page size; `cnvdbfl -c -f 8.3.8 -p 16k <file>`; or dump/restore .dt. [A: its.1c.ru/db/content/metod8dev/src/admins/file_variant/i8105924.htm; kb.1ci.com cnvdbfl page (not fetched, via search snippet)]. Page-size limits 4/8/16/32/64 KB -> object max about 4 GB .. 16 TB (2^(3n-4)). [A: AWA838]

Page 0 header, page 1 = free-pages object, page 2 = root object. [A: AWA838; S: T1CD `new V8Object(this,1/2)`]

Object layouts:

| | 8.2.14 and older | 8.3.8 |
|---|---|---|
| Header page | sig `"1CDBOBV8"`(8), u32 len, u32 ver1, ver2, ver3, then u32 blocks[1018] | sig `1C FD` (data) or `1C FF` (free list), u16 fat_level, u32 ver1..3, **u64 len** at offset 16, then u32 pages[(pagesize-24)/4] (1018 for 4K) [S: T1CD V8Object.h `v838ob_data`; DTOOLS database_reader.py] |
| Allocation | blocks[] = index pages; each index page = i32 count + u32 data[1023]; capacity per header page 1018*1023 pages | fat_level 0: pages[] are data pages directly; fat_level 1: pages[] are index pages holding pagesize/4 data page numbers (zero terminated) [S: DTOOLS] |
| Free object | ver3 field == 0; length = number of free pages; entries 1024/page | sig `1C FF`; entries not in order, taken from the end of list [A: AWA, AWA838] |
| Page address in table desc / index | byte offset (index files: offsets multiple of 0x1000) | page number (multiply by page size) [S: T1CD Index.cpp, Table.cpp] |
| Root object (page 2) | stream: lang[8 or 32], i32 numblocks, i32 blocks[]; each = page number of an object holding the table description as **UTF-16** text | root stream is a 256-byte-chunk blob chain (same as table blobs); `lang[32]`, i32 numblocks, i32 chunk numbers; table descriptions are **UTF-8** blobs [S: T1CD Class_1CD.cpp; DTOOLS root_object; V for 8.3.8 via raw1cd] |

A 8.2.14 reader must: use fixed 4096 pages, header-page + index-page allocation, UTF-16 table descriptions via separate objects, no page-size field; Fields/Files semantics, records and blobs are otherwise identical (T1CD uses one Table class for all). [S]. 8.2.14 and 8.0/8.1 paths are **not verified on a file** (no sample).

## 2. Record layout [S: T1CD Table.cpp, FieldType.cpp; DTOOLS; AWA]

Row = `[state byte][fields...]`. Row length = max(5, 1 + sum field sizes). Data object length is a multiple of row length. [V: all 6092 tables]

- State byte 1 = free/deleted slot; bytes 1..4 = i32 index of next free row. State 0 = live. Row 0 is always a free-list head (next free index, 0 = none). Deleted-record content is not guaranteed to be preserved: ignore. [S: T1CD delete_data_record; AWA]
- Table description grammar: `{"Name",0,{"Fields",{"_F","T",nullflag,length,precision,"CS|CI"},...},{"Indexes",...},{"Recordlock","0|1"},{"Files",data,blob,index}}`; Files = object header pages (0 = none). [AWA; V]
- Field order in row: **all RV fields first, then others in description order**. If Recordlock = 1 and there is no RV field, a hidden 8-byte "short version" sits first (T1CD adds a hidden `VERSION` field of size 8). [S: Table.cpp 295, 420-436; AWA]. raw1cd/DTOOLS ignore this: **not verified**, no table with Recordlock=1 in the demo file (all 6092 have 0). An importer must refuse or handle Recordlock=1 tables explicitly.
- Null flag: if NullExists=1 the field has a leading byte, 0 = NULL, else value. [AWA; V (raw1cd works)]

| Type | Size (bytes) | Encoding |
|---|---|---|
| B | length | raw bytes (GUIDs are B,16) |
| L | 1 | 0 false else true |
| N | (length+2)>>1 | BCD nibbles: first nibble = sign (**0 negative, non-zero (1) positive**), then `length` digits; point `precision` digits from right; trailing pad nibble if length even [S: T1CD BinaryDecimalNumber.cpp; AWA example 0x18 47 23 = 84.723 for length 5/prec 3 (as sign=1, digits 84723)] |
| NC | length*2 | UTF-16LE fixed (space padded) |
| NVC | length*2+2 | u16 char count, then UTF-16LE |
| RV | 16 | four i32 (version 1/2/3-ish); [A] semantics unknown |
| NT | 8 | u32 first chunk, u32 byte length in blob object; data UTF-16LE |
| I | 8 | same, raw bytes |
| T | 8 | accepted by T1CD as "text", not seen in demo file [S] |
| VB | length+2 | var binary, [S: T1CD get_size]; not seen [NR] |
| DT | 7 | BCD `yyyyMMddhhmmss` as 7 bytes; year 0000 = empty (raw1cd decode_datetime) [S: T1CD BinaryDecimalDate; V] |

GUID byte order: text `AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE` is stored as bytes `DDDD EEEEEEEEEEEE CCCC BBBB AAAAAAAA` i.e. text = bytes[12:16]-[10:12]-[8:10]-[0:2]-[2:8], each group in stored order. [S: T1CD GUIDas1C; AWA838; V: Enum _IDRRef equals CONFIG value GUIDs]. Field names are upper-case in table descriptions (`_IDRREF`), match case-insensitively. [V]

Blob object: 256-byte chunks `[u32 next][u16 len<=250][250 data]`; chunk 0 is the free-chain head (never data); field gives first chunk index and total length; empty blob may have no pages. [S: T1CD V8Object::readBlob; AWA; V]

Sources: T1CD src/tool1cd/Table.cpp, FieldType.cpp, BinaryDecimalNumber.cpp, V8Object.cpp; AWA; AWA838; DTOOLS database_reader.py.

## 3. ValueStorage (ХранилищеЗначения) [V on 8 blobs classes below]

Two stored forms (first two bytes), same body:

```
body      = u64LE L | L bytes of UTF-8 text (starts EF BB BF, "{"#",<type-guid>,...}" serialised value)
            | zero or more binary segments: u32 0 | u64LE n | n bytes
form 01 01: 01 01 | body                        (uncompressed)
form 02 01: 02 01 | 16 bytes 53 4B 6F F4 88 8D C1 4E A0 D5 EB B6 BD A0 A7 0D | raw DEFLATE(body)
```

- 02 01: header = **18 bytes** (2 + 16 constant, identical in all 29 samples); inflate `wbits=-15` from offset 18; stream ends cleanly (`eof` true); inflated u64 L matched text length in all 29. The 18-byte skip + raw deflate is also stated by [A: https://infostart.ru/public/1050764/, comment in https://infostart.ru/1c/articles/1320336/ (MSSQL `DECOMPRESS` after cutting 18 bytes)]. Meaning of the 16 bytes unknown.
- Samples: `_REFERENCE264._FLD5059` (7 rows, tail 0, text = Structure with HTML), `_INFORG15747` (_FLD15760/61, 16+ rows, one binary segment starting `EF BB BF 3C`... i.e. an XML/HTML file with BOM), 01 01: `_INFORG14986` (XML segment `<?xml`), `_INFORG15522` (GIF `47494638`, PNG `89504E47`), `_CONST23460` (MOXCEL `4D4F5843` spreadsheet). 4637 further 01 01 values have text only (empty value). 5 values in `_INFORG15522` contain several segments (declared first-segment length != remaining-12): multi-segment layout is inferred, not decoded.
- Text type GUID `4238019d-7e49-4fc9-91db-b6b951d5cf8e` appears as a Structure (pairs "key","value"); GUID `87126200-3e98-44e0-b931-ccb1d7edc497` as a binary-data holder `{1,{0}}`. [V by content; naming inferred]. In metadata Patterns `{"#",e199ca70-93cf-46ce-a54b-6edc88c3a296}` = ValueStorage type, `fc01b5df-97fe-449b-83d4-218a090e681e` = UUID type [S: DAJET model/enums/SingleTypes.cs].
- Other headers: not researched. Exporter rule: parse prefix; 01 01 or 02 01 only; anything else keep raw and flag.

## 4. v8 container (`FF FF FF 7F 00 02 00 00 ...`) [S: DTOOLS container_reader.py; V on 160+ files]

```
0   i32  first free block offset (0x7FFFFFFF = none)
4   i32  default block size (0x200)
8   i32  observed 1 or 2 (meaning unknown)      12 i32 observed 0
16  first document = table of contents
Block (document chunk): CRLF + 8hex(doc size) SP 8hex(this block size) SP 8hex(next block offset|7FFFFFFF) CRLF  = 31 bytes, then data
Document = chain of blocks following next offsets until doc size bytes read
TOC data: repeated 12 bytes: i32 attrs-doc offset, i32 data-doc offset, i32 0x7FFFFFFF
Attrs doc: u64 created, u64 modified (units of 100 microseconds since 0001-01-01), i32 0, UTF-16LE name, 4 zero bytes (strip at first NUL)
```
Verified on `_SYSTEMSETTINGS._SETTINGSDATA` (157 values; one entry `$Stream$`, body plain BOM text) and CONFIG `<guid>.<n>` files (entries `info`+`text` (module), or `form`+`module`; plain, not deflated, because the whole CONFIG row is already deflated). Standalone .cf/.epf containers deflate each entry (raw deflate) and entries may themselves be containers [S: DTOOLS extract(); v8unpack not read]. CONFIG `.0` of templates start with `MOXC` (`4D4F5843`) [V] instead of a container.

## 5. System tables [V unless noted]

| Table | Columns |
|---|---|
| CONFIG, CONFIGSAVE, CONFIGCAS, PARAMS, FILES | FILENAME NVC128, CREATION DT, MODIFIED DT, ATTRIBUTES N5, DATASIZE N20, BINARYDATA I, PARTNO N10 |
| DBSCHEMA | SERIALIZEDDATA I (plain bracket text with BOM, 3.7 MB: table/field schema) |
| IBVERSION | IBVERSION N10, PLATFORMVERSIONREQ N10 (6, 80312) |
| V8USERS | ID B16, NAME, DESCR, OSNAME(null), CHANGED, ROLESID, SHOW, DATA I, EAUTH, ADMROLE, USSPRH |

- CONFIG row data: raw-deflate (all 42579 files incl. `root`, `version`, `versions`, `<guid>`, `<guid>.N`, `.1c`). PARAMS: `*.si` and `dbnames` deflated; `*.ui`, `dbnamesversion*`, `*.inf`, `siversions` plain. Multipart: same FILENAME, PARTNO 0..n, concatenate parts then inflate (first part 10,000,000 bytes seen; 8 multipart files). FILES holds extension/CAS and `.pfl` user-profile blobs [names only].
- `root` = `{2,<config-object-guid>,}`; `version` = `{{216,0,{1,0}}}`; `versions` = `{1,<count>,"",<guid>,"<hash>",...}` (file name -> version id pairs; 3.2 MB). [V]
- DBNames (PARAMS): `{<total>,{<count>,{<guid>,"Prefix",<number>},...}}`; guid is the metadata object (or 0 for settings tables), number is the table/field number in table names. A guid may have several entries (main + service tables). 32335 entries here; **do not trust the first number**. [V; S: DAJET DBNames.cs reads `[2][1]` as count]
- DBNames prefixes seen (count) [V]: Reference 363, Document 268, Enum 643, Chrc 8, Acc 1, AccRg 1, InfoRg 648, AccumRg 83, Const 397, Node 25, CKinds 3, DocumentJournal 24, Seq 5, ScheduledJobs 75, VT 955, LineNo 955 (tabular section line column), Fld 22541 (attribute/dimension/resource/column), ExtDim, Turnover/TurnoverDt/TurnoverCt (AT resource columns), service-table prefixes ReferenceChngR, DocumentChngR, InfoRgChngR, AccRgChngR, AccumRgChngR, ChrcChngR, AccChngR, CKindsChngR, ConstChngR, SeqChngR, SeqB, InfoRgSL, InfoRgOpt, AccumRgT, AccumRgTn, AccumRgOpt, AccumRgAggDict*, AccumRgAggGridK, AccumRgAggOptK, AccumRgBfK, AccumRgDlK, AccumRgSt, AccRgED, AccRgAT0..AT3, AccRgCT, AccRgOpt, RefOpt, RefSInf, ChrcOpt, ChrcSInf, CKindsOpt, CKindsSInf, AccOpt, AccSInf, Consts, DataSeparationUse, index-name tokens ByField/ByDims/ByProperty/ByResource/ByParentField/ByOwnerField/ByDim, settings-store tokens (SystemSettings, CommonSettings, RepSettings...), DataHistory*, Extensions*. Not present in this base but defined: BPr, BPrPoints, Task, CRg*, CKind sub-tables [S: DAJET loader/MetadataTokens.cs]. Official table list: ITS-FIELDS, https://its.1c.ru/db/content/metod8dev/src/admins/ (and https://kb.1ci.com/1C_Enterprise_Platform/FAQ/Administration/DBMS/Data_structure_in_1C_Enterprise_8/ : constants prefix is `_Consts` in table names; `_Const<n>` per constant here).

## 6. Metadata tree in CONFIG

Root object file `<guid from root>`: list `[?, [..], count, comp4, comp5, comp6, ...]`. DaJet paths below are **1-based** (verified: Python index = DaJet index - 1). [S: DAJET Configuration.cs; V]

Layout: `[3]` = number of components. Component `[4]` (common) lists classes at `[4][2][k]`... on the real file the class lists sit at Python `tree[3][1][k]` (common), `tree[4][1][1][k]` (operational), `tree[5][1][k]` (accounting), `tree[6][1][k]` (calculation). Each class entry = `{class-guid, N, obj-guid1..N}`; object file = `CONFIG` file named by the object guid.

Class GUIDs (name verified by DBNames kind or by object names; [S: DAJET MetadataTypes.cs] where noted):

| GUID | Class | Count here |
|---|---|---|
| cf4abea6-37b2-11d4-940f-008048da11f9 | Catalogs [S][V] | 363 |
| 061d872a-5787-460e-95ac-ed74ea3a3e84 | Documents [S][V] | 268 |
| f6a80749-5ad7-400b-8519-39dc5dff2542 | Enums [S][V] | 643 |
| 0195e80c-b157-11d4-9435-004095e12fc7 | Constants [S][V] | 396 |
| 13134201-f60b-11d5-a3c7-0050bae0a776 | InformationRegisters [S][V] | 648 |
| b64d9a40-1642-11d6-a3c7-0050bae0a776 | AccumulationRegisters [S][V] | 83 |
| 2deed9b8-0056-4ffe-a473-c20a6c32a0bc | AccountingRegisters [S][V] | 1 |
| 238e7e88-3c5f-48b2-8a3b-81ebbecb20ed | ChartsOfAccounts [S][V] | 1 |
| 82a1b659-b220-4d94-a9bd-14d757b95a48 | ChartsOfCharacteristicTypes [S][V] | 8 |
| 30b100d6-b29f-47ac-aec7-cb8ca8a54767 | ChartsOfCalculationTypes [S][V] | 3 |
| 857c4a91-e5f4-4fac-86ec-787626f1c108 | ExchangePlans [S][V] | 25 |
| 4612bd75-71b7-4a5c-8cc5-2b0b65f9fa0d | DocumentJournals [V] | 24 |
| bc587f20-35d9-11d6-a3c7-0050bae0a776 | Sequences [V] | 5 |
| 11bdaf85-d5ad-4d91-bb24-aa0eee139052 | ScheduledJobs [V] | 75 |
| 631b75a0-29e2-11d6-a3c7-0050bae0a776 | Reports (names) | 424 |
| bf845118-327b-4682-b5c6-285d2a0eb296 | DataProcessors (names) | 213 |
| 09736b02-9cac-4e3f-b4f7-d3e9576ab948 | Roles (names) | 328 |
| 0fe48980-252d-11d6-a3c7-0050bae0a776 | CommonModules (names) | 1571 |
| 07ee8426-87f1-11d5-b99c-0050bae0a95d | CommonForms (probable) | 240 |
| 37f2fa9a-b276-11d4-9435-004095e12fc7 | Subsystems [S] | 36 |
| 15794563-ccec-41f6-a83c-ec5f7b9a5bc1 | SharedProperties [S][V] | 2 |
| c045099e-13b9-4fb6-9d50-fca00202971e | DefinedTypes [S] | 172 |
| 3e63355c-1378-4953-be9b-1deb5fb6bec5 / fcd3404e-1523-48ce-9bc0-ecdb822684a1 | Tasks / BusinessProcesses [S], absent here | 0 |

Other classes present but only guessed from object names: 0c89c792-16c3-11d5-b96b-0050bae0a95d (templates), 7dcd43d9-… (pictures), 4e828da6-… (event subscriptions), 58848766-… (style items), 24c43748-… (session parameters), af547940-… / 30d554db-… (functional options / parameters), 8657032e-… (XDTO packages), 9cd510ce-… (languages), 3e7bfcc0-… (filter criteria), 2f1a5187-…, 1c57eabe-…, 36a8e346-…, cc9df798-…, 0fffc09c-…, 46b4cd97-… (unidentified). Component GUIDs: Common 9cd510cd-abfc-11d4-9434-004095e12fc7, Operations 9fcd25a0-4822-11d4-9414-008048da11f9, Accounting e3687481-0a87-462c-a166-9f34594f9bba, Calculation 9de14907-ec23-4a07-96f0-85521cb6b53b, BusinessProcess 51f2d5d8-ea4d-4064-8892-82951750031e [S: DAJET Components.cs].

Inside an object file (1-based; header node `{ver,{0|1,0,guid},"Name",{1,"ru","Synonym"},...}`) [S: DAJET model/*.cs; V where marked]:

| Object | Item | Path |
|---|---|---|
| Catalog | type (ref) guid | `[2][4]` [V]; object guid `[2][10][2][2][3]`, name `[2][10][2][3]`, synonym `[2][10][2][4]` = `{1,"ru","text"}` [V] |
| | code len / code type / name len | `[2][18]` / `[2][19]` / `[2][20]` [V]; hierarchy kind `[2][37]`, hierarchical flag `[2][38]`; owners `[2][13]` |
| | tabular sections `[6]` (class 932159f9-…), attributes `[7]` (class cf4abea7-…) | [V] |
| Enum | ref type `[2][2]`, name `[2][6][2][3]`; values `[7]` = `{bee0a08c-…,N,item...}` item i (from 3): guid `[7][i][1][2][2][3]`, name `[7][i][1][2][3]` | [V] |
| ChartOfAccounts | type `[2][4]`, name `[2][16][2][3]`, extdim chart guid `[2][20]`, max ext dim `[2][21]` (=3), code len `[2][23]`, name len `[2][24]`, auto-order `[2][25]`/len `[2][26]`; attributes `[8]`, accounting flags `[9]` (class 78bd1243-…), ext-dim accounting flags `[10]` (class c70ca527-…) | [V] |
| AccountingRegister | name `[2][16 or 17][2][3]`, chart guid `[2][19|20]`, correspondence flag `[2][21|22]`, splitter `[2][24|25]`, period adjustment `[2][31]`; dimensions `[4]`, resources `[6]`, attributes `[8]` | [S only] |
| Property item | collection `{class,N,item...}` items from index 3; field guid `[1][2][2][2][2][3]`, name `[1][2][2][2][3]`, type pattern `[1][2][2][3]` | [S; V for Acc flags] |

Type Pattern: `{"Pattern",{"B"},{"S",len,var},{"N",digits,frac,nonneg},{"D"|{"D","D"}|{"D","T"}},{"#",guid}}`; S: second arg 0 = fixed, 1 = variable (len 0/absent = unlimited); N third arg 1 = non-negative; `#` guid = referenced type guid (an object's "generated type" guid from the leading list of its file), UUID type or ValueStorage type. Field -> `_Fld<n>` by the DBNames Fld entry for the property guid [V]. Physical columns: see section 7.

Enum value mapping [V]: `_ENUM<n>._IDRREF` = value guid; `_ENUMORDER` = 0-based definition order. Demo enum 460: 11 of 11 matched.

Predefined data [V]: catalogs with predefined items have CONFIG file `<object-guid>.1c` (119 catalogs here). Bracket file: `{0,{1,{6,<6 column patterns>},{2,6,...},{1,<N>,{2,<n>,6,V0..V5,0},...}}}`; V0 = `{"#",<type>,{1,<guid>}}` equals `_PREDEFINEDID`, V1 B folder flag, V2 parent ref, V3 S **predefined name**, V4 S code, V5 S description. Matched `_PredefinedID` for the checked catalog. Chart of accounts predefined: `<guid>.9` (414 KB, 12 column patterns), characteristic charts `<guid>.7` [structure noted, not decoded]. Other suffixes: `.5` help HTML, `.0/.1/.2/.3` forms/modules/templates containers (per class) [V sizes only].

## 7. Physical column conventions

Official: [ITS-FIELDS] and https://its.1c.ru/db/content/metod8dev/src/developers/platform/metod/other/i8101828.htm ; code view: DAJET model/base/Configurator.cs.

Composite type suffixes [A: ITS i8101828 snippet; S: DAJET ColumnPurpose.cs, Union.cs]: `_TYPE` (B1): 01 undefined/NULL, 02 boolean (`_L` B1), 03 number (`_N`), 04 date (`_T`), 05 string (`_S`), 06 binary (`_B`), 08 reference (`_RTRef` B4 + `_RRRef` B16); a field holding exactly one reference type has only `_<fld>RRef` (no TYPE). `_RTRef` = DBNames number of the referenced object's table, 4 bytes big-endian; `00000000` for none. [V: `_AccRg891._RecorderTRef` 0x189 = Document 393 in DBNames]. `_Fld<n>` bare = non-composite value.

Standard columns [ITS-FIELDS]: `_Reference<n>`: `_IDRRef`, `_Version` (RV), `_Marked`, `_PredefinedID`, `_ParentIDRRef` (hierarchical), `_OwnerID*`, `_Folder`, `_Code`, `_Description`, `_Fld*`. Doc says `_Folder` = "is group"; in practice 1C stores it inverted (0x01 = element, 0x00 = group) **[A, per project brief; not checked here, not researched]**. `_Document<n>`: `_IDRRef,_Version,_Marked,_Date_Time,_NumberPrefix,_Number,_Posted`. Tabular section `_X<n>_VT<k>`: `_X<n>_IDRRef`, `_KeyField` (B4), `_LineNo<n>`, `_Fld*`. `_Enum`: `_IDRRef,_EnumOrder`. `_Chrc`: as catalog plus `_Type` (type description). `_Acc<n>`: `_Kind` (account kind), `_OffBalance`, `_OrderField`; `_Acc<n>_ExtDim<k>`: `<acc>_IDRRef,_KeyField,_LineNo,_DimKindRRef,_DimIsMetadata,_TurnoverOnly,_Fld*(ext-dim flags)` [V]. `_Node`: `_SentNo,_ReceivedNo`. `_DocumentJournal`: `_Document*,_Date_Time,_Marked,_Posted,_Number`. `_Seq`, `_SeqB`: `_Period,_Recorder*`. `_Const<n>` table here, `_Consts` = options.

Registers [V on `_AccRg891`, `_AccRgED928`, `_AccRgAT0905/AT1924`, `_AccRgCT927`, `_AccRgOpt930`; ITS-FIELDS]:
- `_AccRg`: `_Period, _RecorderTRef(4), _RecorderRRef(16), _LineNo N9, _Active L, _AccountDtRRef, _AccountCtRRef` (with correspondence), dimensions `_FldNRRef` or `_FldN{Dt|Ct}RRef` (non-balance dims split Dt/Ct, nullable), resources `_FldN` (balance) or `_FldN{DT|CT}` (non-balance split), attributes, `_EDHashDt/_EDHashCt` N10. Correspondence off: `_Account*RRef` single and `_Correspond`. [S: DAJET AccountingRegister.cs]
- `_AccRgED`: `_Period,_Recorder*,_LineNo,_Correspond N1 (with correspondence: 0 debit/1 credit [S]),_KindRRef,_Value_TYPE,_Value_RTRef,_Value_RRRef` (+ `_Value_L/N/T/S` as the chart's value types). Link to movement: same `_Period,_Recorder,_LineNo`. From platform compat 8.3.15 values also in main table `_Kind{Dt|Ct}<i>RRef`/`_Value{Dt|Ct}<i>_*` [S: Configurator.cs]; not in this base.
- `_AccRgAT<k>`: `_AccountRRef,_Period,<dims>,_Value<i>_TYPE/_RTRef/_RRRef (k>=1),<resource>,_TurnoverDt<n>,_TurnoverCt<n>,_Turnover<n>,_Splitter`. Totals period for current totals rows = **3999-11-01 00:00:00** [V]; Splitter 0 [V]. `_AccRgCT`: `_Period,_AccountDt/Ct,dims/res (Dt/Ct split),_Splitter`. `_AccRgOpt`: `_RegID,_Period,_ActualPeriod,_Periodicity,_RepetitionFactor,_UseTotals,_MinPeriod,_UseSplitter,_MinCalculatedPeriod` [V].
- `_AccumRg`: `_Period,_Recorder*,_LineNo,_Active,_RecordKind (0 receipt,1 expense [S: Configurator.cs ConfigurePropertyВидДвиженияНакопления; value meaning not checked]),dims/res/attrs,_DimHash`; `_AccumRgT` balances `_Period,..,_Splitter,_DimHash`; `_AccumRgTn` turnovers.
- `_InfoRg`: `_Period` (periodic only), `_Recorder*,_LineNo,_Active` only if recorder-subordinate, `_Fld*`; `_InfoRgSL/SF` slices.
- ChngR tables: `_Node*,_MessageNo,<key>`; irrelevant for export.

## 8. V8USERS [V structure, S: DTOOLS examples/users.py]
DATA (I): byte0 = mask length m (30 here), bytes 1..m = mask, remaining bytes XORed cyclically with the mask; result UTF-8 BOM bracket text. Here: `{<guid>,"<name>","<sha-ish>",...,{0},<guid>,...,<date>...}`. Password hashes (base64 SHA1 pair) are in it; do not export. Business use: none (rights/roles only); skip. `_SYSTEMSETTINGS/_COMMONSETTINGS._SETTINGSDATA` are v8 containers with `$Stream$` text [V]; user UI settings only.

## 9. Offline-reader pitfalls
- Read a consistent copy (stopped base or a copy); page-level read of a live file can see half-written objects [common sense, not sourced]. File-mode directory: `1Cv8.1CD` data, `1Cv8Log` event log (.lgf), `1Cv8FTxt` full-text index [A: forum.infostart.ru and 1c-dn snippets]. Name/behaviour of the `1Cv8.1CL` lock file: **not found / not researched**.
- Data separation: base has shared property `ОбластьДанныхОсновныеДанные` -> `_Fld878` N7 (area, 0 here) on most tables; `ОбластьДанныхВспомогательныеДанные` -> `_Fld932`; `_DataSeparationUse933/934` flags in settings tables [V]. Filter/ignore explicitly; keep it in key tuples if non-zero.
- Extensions: `_ExtensionsInfo*`, `ConfigCAS`, borrowed objects have no own DBNames codes [S: DAJET]. `_DataHistory*` tables exist (versions empty here) [V]. CONFIGSAVE empty (no unsaved config) [V]. DBSCHEMA exists as full schema [V].
- Attached-file volumes on disk (Тома), full-text index files: **not researched**.
- Large files: `<guid>.0` etc. multipart; CONFIG blob object ~0.5 GB, read chain-wise.

## 10. Licences
| Project | Licence | Reuse in MIT/Apache project |
|---|---|---|
| e8tools/Tool1CD | COPYING = GPL v3; source headers state LGPL v3 for the library | No copying; reimplement from format knowledge only |
| Infactum/onec_dtools | MIT (LICENSE) | Yes, with notice |
| zhichkin/dajet-metadata | MIT | Yes, with notice (C#; port logic) |
| e8tools/v8unpack | MPL 2.0 (LICENSE) | File-level copyleft; avoid copying |
| vendor/onecreader (1creader) | MIT (repo copy) | Yes |
| onec-converter, soxoj/1c-database-converter | not researched | - |
Facts about formats are not copyrightable; this note restates them in our words.

## Not researched / unknown
8.0/8.1/8.2 sample files; Recordlock=1 records; `_Folder` inversion check; 16-byte ValueStorage header meaning; multi-segment ValueStorage layout; container header ints 3/4; remaining class GUIDs; Roles/Reports internals; `.7/.9` predefined formats for charts; lock files, volumes, full-text; BPr/Task/CRg tables; v8unpack, 1c-syntax/mdclasses.
