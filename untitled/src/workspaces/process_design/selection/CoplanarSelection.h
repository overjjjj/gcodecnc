#pragma once
#include "SelectionCandidate.h"
#ifdef CNEXT_ENABLE_OCC
#include <BRepAdaptor_Surface.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <Standard_Failure.hxx>
#endif

// Source planes use the same normalized model coordinates as the imported mesh.
inline QMap<int, SelectionSourcePlane> selectionSourcePlanes(const MeshData &mesh)
{
    QMap<int, SelectionSourcePlane> result;
#ifdef CNEXT_ENABLE_OCC
    if (mesh.sourceShape.IsNull()) return result;
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(mesh.sourceShape, TopAbs_FACE, faces);
    for (int i = 1; i <= faces.Extent(); ++i) {
        try {
            const auto face = TopoDS::Face(faces(i));
            BRepAdaptor_Surface surface(face);
            if (surface.GetType() != GeomAbs_Plane) continue;
            const auto plane = surface.Plane();
            auto normal = plane.Axis().Direction();
            if (face.Orientation() == TopAbs_REVERSED) normal.Reverse();
            SelectionSourcePlane proof;
            proof.verified = true;
            proof.normal = QVector3D(normal.X(), normal.Y(), normal.Z());
            proof.offset = plane.Location().XYZ().Dot(normal.XYZ());
            result.insert(i, proof);
        } catch (const Standard_Failure &) {
            // A face without exact plane evidence is not eligible for batch selection.
        }
    }
#else
    Q_UNUSED(mesh);
#endif
    return result;
}

inline bool selectionPlanesCoplanar(const SelectionSourcePlane &a,
                                    const SelectionSourcePlane &b)
{
    return a.verified && b.verified &&
        (a.normal - b.normal).lengthSquared() < 1e-12f &&
        std::isfinite(a.offset) && std::isfinite(b.offset) &&
        std::abs(a.offset - b.offset) <= 1e-5;
}
