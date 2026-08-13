# Third-Party Notices — IFC Importer (MobiusIfcBridge)

This file lists the third-party software components that are compiled into
`MobiusIfcBridge.dll` and redistributed with Project Mobius. All of these
components are built from source vendored under
`Source/ThirdParty/IfcBridgeSource/` in this repository, and their object
code ships inside the DLL in binary form. This notice satisfies the
attribution and licence-reproduction obligations of the permissive licences
listed below.

Audit date: 2026-08-11. Document date: 2026-08-12. Every licence text quoted
below was read from the file on disk in the vendored tree at the path given
in the "Path in tree" column; none of it was reproduced from memory or from
an external source.

## Component table

| Component | Version / commit | Licence | Copyright | Upstream | Path in tree |
|---|---|---|---|---|---|
| IfcPlusPlus (IFC++) | recorded as commit `7b80900` per the prior audit; not independently verifiable — the vendored tree has no `.git` metadata or version file | MIT | Copyright (c) 2010-2015 Fabian Gerold (fbnge4@gmail.com) | https://github.com/ifcquery/IfcPlusPlus | `IfcPlusPlus/LICENSE.txt` |
| Carve (CSG kernel) | not recorded in tree | MIT — **relicensed 2015; see note below** | Copyright 2006-2015 Tobias Sargeant (tobias.sargeant@gmail.com) | http://carve-csg.com/ | Header block in `IfcPlusPlus/src/external/Carve/src/include/carve/carve.hpp` (no standalone LICENSE file ships in this tree) |
| glm | not recorded in tree | MIT ("Happy Bunny" variant; plain MIT also offered) | Copyright (c) 2005 G-Truc Creation | https://github.com/g-truc/glm | `IfcPlusPlus/src/external/glm/copying.txt` |
| earcut / earcut.hpp | not recorded in tree | ISC | Copyright (c) 2015, Mapbox | https://github.com/mapbox/earcut.hpp | `IfcPlusPlus/src/external/earcut/LICENSE` |
| RapidJSON | not recorded in tree | MIT | Copyright (C) 2015 THL A29 Limited, a Tencent company, and Milo Yip | https://github.com/Tencent/rapidjson | Header block in `IfcPlusPlus/src/external/RapidJSON/rapidjson.h` (no standalone LICENSE file ships in this tree) |
| nowide | not recorded in tree | Boost Software License 1.0 | Copyright (c) 2012 Artyom Beilis (Tonkikh) | historically https://github.com/artyom-beilis/nowide; folded into Boost as Boost.Nowide (https://github.com/boostorg/nowide) | Header block in `IfcPlusPlus/src/external/nowide/config.hpp` (the header points to an accompanying `LICENSE_1_0.txt`, but no such file ships in this tree — see "Licence texts" below for the text it refers to) |
| utf8 (utfcpp) | not recorded in tree | Permissive custom licence, textually identical (minus title line) to the Boost Software License 1.0 body — see note below | Copyright 2006-2016 Nemanja Trifunovic | https://github.com/nemtrif/utfcpp | Header block in `IfcPlusPlus/src/external/utf8/checked.h` (no standalone LICENSE file ships in this tree) |
| zippy (header-only zip wrapper, embeds miniz 2.0.8) | not recorded in tree | Public domain (Unlicense), with an accompanying MIT-style notice covering the same embedded code | Copyright 2013-2014 RAD Game Tools and Valve Software; Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC; Copyright 2016 Martin Raiber | not recorded in tree; not independently verified | Header blocks embedded in `IfcPlusPlus/src/external/zippy/zippy.hpp` (no standalone LICENSE file ships in this tree) |
| zip-master — zip wrapper (`zip.c` / `zip.h`) | not recorded in tree | MIT-style — **incomplete in this tree, see finding below** | Not stated in the vendored files | likely https://github.com/kuba--/zip (the folder name `zip-master` matches the name GitHub gives a default-branch zip download of that repository; this has not been independently confirmed) | `IfcPlusPlus/src/external/zip-master/zip.h`, `IfcPlusPlus/src/external/zip-master/zip.c` (no standalone LICENSE file ships in this tree) |
| zip-master — miniz (`miniz.h`) | 2.2.0 per the in-file header comment | Public domain (Unlicense), with an accompanying MIT-style notice covering the same embedded code | Copyright 2013-2014 RAD Game Tools and Valve Software; Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC; Copyright 2016 Martin Raiber | https://github.com/richgel999/miniz | `IfcPlusPlus/src/external/zip-master/miniz.h` (no standalone LICENSE file ships in this tree) |

## Note on Carve's licence history

Carve was GPL-2.0 licensed prior to 2015. A number of older forks and
third-party copies of Carve (including forks commonly seen under names such
as "VTREEM/Carve") still carry that GPL-2.0 text and this leads to a
widespread — but outdated — claim online that "Carve is GPL." Carve was
relicensed to MIT in 2015. The copy vendored in this repository, at
`IfcPlusPlus/src/external/Carve/src/include/carve/carve.hpp`, carries the
post-relicense header: "Copyright 2006-2015 Tobias Sargeant
(tobias.sargeant@gmail.com)" followed by the standard MIT permission and
disclaimer text (see "Licence texts" below). **The Carve code in this
repository is MIT-licensed, not GPL.** This note exists so a future reviewer
does not re-raise the GPL claim without checking the actual vendored header.

## Finding: incomplete licence header on the zip-master wrapper

The `zip.c` / `zip.h` wrapper (as distinct from the `miniz.h` it wraps) is
vendored with only a fragment of its licence notice. Both files begin with
the standard MIT-style warranty disclaimer paragraph ("THE SOFTWARE IS
PROVIDED 'AS IS' … ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
THE USE OR OTHER DEALINGS IN THE SOFTWARE.") but are **missing the copyright
holder line and the grant-of-permission clause** that normally precede that
disclaimer in an MIT licence block. No copyright line and no separate
LICENSE file for this component exist anywhere else in the vendored tree.
This is disclosed as-is; the disclaimer text on disk is consistent with an
MIT-style permissive licence, but a defensible reproduction of "the MIT
licence" for this specific component cannot be sourced complete from this
repository. If a definitive upstream licence file for this component is
later located, this entry should be corrected to link to it.

## Licence texts

Each licence below is reproduced in full exactly once. The component table
above states which text applies to each component and gives that
component's specific copyright line.

### MIT License

Used by (with each component's own copyright line as listed in the table
above): IfcPlusPlus, Carve, RapidJSON, and — as one of two dual-licensed
options — glm.

> MIT License
>
> Copyright (c) \<year\> \<copyright holder — see table above for the exact
> line for each component\>
>
> Permission is hereby granted, free of charge, to any person obtaining a
> copy of this software and associated documentation files (the
> "Software"), to deal in the Software without restriction, including
> without limitation the rights to use, copy, modify, merge, publish,
> distribute, sublicense, and/or sell copies of the Software, and to permit
> persons to whom the Software is furnished to do so, subject to the
> following conditions:
>
> The above copyright notice and this permission notice shall be included
> in all copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
> THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
> FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
> DEALINGS IN THE SOFTWARE.

### The Happy Bunny License (Modified MIT License)

Used by: glm, as its primary offered licence (glm's own `copying.txt`
grants the recipient a choice between this text and the plain MIT text
above; both carry the same copyright line, "Copyright (c) 2005 G-Truc
Creation"). Reproduced here verbatim, including its non-standard
"Restrictions" clause, from `glm/copying.txt`:

> The Happy Bunny License (Modified MIT License)
>
> Copyright (c) 2005 - G-Truc Creation
>
> Permission is hereby granted, free of charge, to any person obtaining a
> copy of this software and associated documentation files (the
> "Software"), to deal in the Software without restriction, including
> without limitation the rights to use, copy, modify, merge, publish,
> distribute, sublicense, and/or sell copies of the Software, and to permit
> persons to whom the Software is furnished to do so, subject to the
> following conditions:
>
> The above copyright notice and this permission notice shall be included
> in all copies or substantial portions of the Software.
>
> Restrictions:
>  By making use of the Software for military purposes, you choose to make
>  a Bunny unhappy.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
> THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
> FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
> DEALINGS IN THE SOFTWARE.

Because glm's own file offers the plain MIT License above as an equally
valid alternative to this text, glm's obligations are satisfiable under
either licence text reproduced in this document.

### ISC License

Used by: earcut / earcut.hpp.

> ISC License
>
> Copyright (c) 2015, Mapbox
>
> Permission to use, copy, modify, and/or distribute this software for any
> purpose with or without fee is hereby granted, provided that the above
> copyright notice and this permission notice appear in all copies.
>
> THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
> WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
> MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
> ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
> WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
> ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR
> IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

### Boost Software License - Version 1.0

Used by: nowide (Copyright (c) 2012 Artyom Beilis (Tonkikh)) directly, and
by utf8/utfcpp in substance — see the note on utf8/utfcpp immediately below
the text.

> Boost Software License - Version 1.0 - August 17th, 2003
>
> Permission is hereby granted, free of charge, to any person or
> organization obtaining a copy of the software and accompanying
> documentation covered by this license (the "Software") to use, reproduce,
> display, distribute, execute, and transmit the Software, and to prepare
> derivative works of the Software, and to permit third-parties to whom the
> Software is furnished to do so, all subject to the following:
>
> The copyright notices in the Software and this entire statement,
> including the above license grant, this restriction and the following
> disclaimer, must be included in all copies of the Software, in whole or
> in part, and all derivative works of the Software, unless such copies or
> derivative works are solely in the form of machine-executable object code
> generated by a source language processor.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
> SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
> FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR
> OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
> USE OR OTHER DEALINGS IN THE SOFTWARE.

**Note on utf8/utfcpp:** the licence header actually vendored at the top of
`IfcPlusPlus/src/external/utf8/checked.h` (Copyright 2006-2016 Nemanja
Trifunovic) does not name the Boost Software License, but its operative
text — the permission grant, the notice-preservation condition, and the
disclaimer — is word-for-word identical to the Boost Software License 1.0
body quoted above (it simply omits the "Boost Software License - Version
1.0" title line and is not badged as Boost). It is listed as its own row in
the component table, with its own copyright line, but no separate licence
text is duplicated for it: the text above is that text.

### The Unlicense

Used by: zippy (embedded miniz 2.0.8) and zip-master/miniz.h (miniz 2.2.0),
alongside the MIT-style notice below for the same code.

> This is free and unencumbered software released into the public domain.
>
> Anyone is free to copy, modify, publish, use, compile, sell, or
> distribute this software, either in source code form or as a compiled
> binary, for any purpose, commercial or non-commercial, and by any means.
>
> In jurisdictions that recognize copyright laws, the author or authors of
> this software dedicate any and all copyright interest in the software to
> the public domain. We make this dedication for the benefit of the public
> at large and to the detriment of our heirs and successors. We intend this
> dedication to be an overt act of relinquishment in perpetuity of all
> present and future rights to this software under copyright law.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
> THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
> IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
> CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.
>
> For more information, please refer to <http://unlicense.org/>

Both vendored copies of this text (in `zippy.hpp` and in `zip-master/miniz.h`)
additionally carry, immediately following it, the same MIT-style notice
reproduced under "MIT License" above, naming "RAD Game Tools and Valve
Software" (2013-2014), "Rich Geldreich and Tenacious Software LLC"
(2010-2014), and, on later blocks in both files, "Martin Raiber" (2016) as
copyright holders. Both the public-domain dedication and the MIT-style
grant are present for this code; either is independently sufficient to
permit its use, and neither is copyleft.

## Statement on copyleft

No component identified in the vendored `Source/ThirdParty/IfcBridgeSource/`
tree is licensed under the GNU General Public License (GPL), GNU Lesser
General Public License (LGPL), GNU Affero General Public License (AGPL), or
Mozilla Public License (MPL), or any other copyleft licence, at the revision
audited. Every component listed above is permissively licensed (MIT, ISC,
Boost Software License 1.0, or public domain / Unlicense).
