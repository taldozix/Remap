class_name MeshBatcher
extends RefCounted
## Junta muitas caixas/cilindros estáticos coloridos em uma única malha (uma chamada de
## desenho). Usado para mobília decorativa, marcações de rua, cercas, meio-fios etc.

var st := SurfaceTool.new()
var count := 0

func _init() -> void:
	st.begin(Mesh.PRIMITIVE_TRIANGLES)

func add_box(xform: Transform3D, size: Vector3, col: Color) -> void:
	var h := size * 0.5
	var corners := [
		Vector3(-h.x, -h.y, -h.z), Vector3(h.x, -h.y, -h.z), Vector3(h.x, h.y, -h.z), Vector3(-h.x, h.y, -h.z),
		Vector3(-h.x, -h.y, h.z), Vector3(h.x, -h.y, h.z), Vector3(h.x, h.y, h.z), Vector3(-h.x, h.y, h.z),
	]
	var faces := [
		[[4, 5, 6, 7], Vector3(0, 0, 1)], [[1, 0, 3, 2], Vector3(0, 0, -1)],
		[[5, 1, 2, 6], Vector3(1, 0, 0)], [[0, 4, 7, 3], Vector3(-1, 0, 0)],
		[[7, 6, 2, 3], Vector3(0, 1, 0)], [[0, 1, 5, 4], Vector3(0, -1, 0)],
	]
	var basis := xform.basis
	for f in faces:
		var idx: Array = f[0]
		var n: Vector3 = (basis * f[1]).normalized()
		var shade := 1.0
		var v := [xform * corners[idx[0]], xform * corners[idx[1]], xform * corners[idx[2]], xform * corners[idx[3]]]
		for tri in [[0, 2, 1], [0, 3, 2]]:
			for k in tri:
				st.set_color(col * shade)
				st.set_normal(n)
				st.add_vertex(v[k])
	count += 1

## Atalho: caixa alinhada aos eixos com rotação Y (graus) e centro.
func box(center: Vector3, size: Vector3, col: Color, rot_deg: float = 0.0) -> void:
	add_box(Transform3D(Basis(Vector3.UP, deg_to_rad(rot_deg)), center), size, col)

func add_cylinder(center: Vector3, radius: float, height: float, col: Color, segments: int = 8) -> void:
	var h := height * 0.5
	for i in segments:
		var a0 := TAU * i / segments
		var a1 := TAU * (i + 1) / segments
		var p0 := Vector3(cos(a0) * radius, 0, sin(a0) * radius)
		var p1 := Vector3(cos(a1) * radius, 0, sin(a1) * radius)
		var n := Vector3(cos((a0 + a1) * 0.5), 0, sin((a0 + a1) * 0.5))
		var quad := [center + p0 + Vector3(0, -h, 0), center + p1 + Vector3(0, -h, 0), center + p1 + Vector3(0, h, 0), center + p0 + Vector3(0, h, 0)]
		for tri in [[0, 1, 2], [0, 2, 3]]:
			for k in tri:
				st.set_color(col)
				st.set_normal(n)
				st.add_vertex(quad[k])
		for tri in [[center + Vector3(0, h, 0), center + p0 + Vector3(0, h, 0), center + p1 + Vector3(0, h, 0)]]:
			for v in tri:
				st.set_color(col)
				st.set_normal(Vector3.UP)
				st.add_vertex(v)
	count += 1

func add_sphere(center: Vector3, radius: Vector3, col: Color, rings: int = 5, segments: int = 8) -> void:
	for r in rings:
		var t0 := PI * r / rings
		var t1 := PI * (r + 1) / rings
		for i in segments:
			var a0 := TAU * i / segments
			var a1 := TAU * (i + 1) / segments
			var pts := [_sph(t0, a0), _sph(t0, a1), _sph(t1, a1), _sph(t1, a0)]
			var verts: Array = []
			for q in pts:
				verts.append(center + q * radius)
			for tri in [[0, 2, 1], [0, 3, 2]]:
				for k in tri:
					st.set_color(col)
					st.set_normal(pts[k])
					st.add_vertex(verts[k])
	count += 1

func _sph(theta: float, phi: float) -> Vector3:
	return Vector3(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi))

func build(roughness: float = 0.85, shadows: bool = true) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	if count == 0:
		return mi
	mi.mesh = st.commit()
	mi.material_override = Mat.vertex_color(roughness)
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON if shadows else GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	return mi
