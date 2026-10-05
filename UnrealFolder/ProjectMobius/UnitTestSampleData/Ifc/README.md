# IFC test data

`WallWithOpening_IFC4.ifc` — a minimal IFC4 model: one `IfcWall` (explicit faceted B-rep) with an
`IfcOpeningElement` joined by `IfcRelVoidsElement`, plus the usual spatial containers (`IfcProject`,
`IfcSite`). Small (≈4 KB, 100 STEP lines) and fast to parse, but real enough to exercise the whole
IFC path: STEP read → schema detection → IfcPlusPlus geometry conversion → Carve triangulation → the
render allowlist (the wall renders; the opening volume is dropped).

Used by `Source/ProjectMobiusTests/Private/Tests/IfcMeshLoaderTest.cpp`.

Note on the opening: the wall renders **solid** — the window hole is *not* cut. IfcPlusPlus does run
the full subtraction path (inverse attributes are resolved and `GeometryConverter::subtractOpenings`
is called for the wall), but Carve's CSG boolean does not cut this particular pair of hand-authored
`IfcFacetedBrep` boxes, so IfcPlusPlus falls back to the wall's un-subtracted mesh (12 tris) and the
opening comes back as its own volume that the render allowlist drops. This is a CSG-robustness edge
of this synthetic file, not a loader bug and not platform-specific — real BIM-exported IFC files carry
the openings already booleaned into the wall's Body representation and render with the hole present.

Provenance: copied verbatim from the vendored IfcPlusPlus checkout
(`Source/ThirdParty/IfcBridgeSource/IfcPlusPlus/examples/LoadFileExample/example.ifc`), which ships
under IfcPlusPlus's MIT license.
