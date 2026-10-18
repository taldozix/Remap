class_name MeshBatcher
extends RefCounted
## Junta muitas caixas/cilindros estáticos em uma única malha. Cada material (chave de
## Mat.named, ex.: "brick", "wood", "steel"; "" = só cor de vértice) vira uma superfície;
## a cor de vértice tinge a textura. Usado para paredes, mobília, mobiliário urbano etc.

var surfaces: Dictionary = {}   # chave do material -> SurfaceTool
var count := 0

func _st(mat: String) -> SurfaceTool:
	if not surfaces.has(mat):
		var st := SurfaceTool.new()
		st.begin(Mesh.PRIMITIVE_TRIANGLES)
		surfaces[mat] = st
	return surfaces[mat]

func add_box(xform: Transform3D, size: Vector3, col: Color, mat: String = "") -> void:
	var st := _st(mat)
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
		var v := [xform * corners[idx[0]], xform * corners[idx[1]], xform * corners[idx[2]], xform * corners[idx[3]]]
		for tri in [[0, 2, 1], [0, 3, 2]]:
			for k in tri:
				st.set_color(col)
				st.set_normal(n)
				st.add_vertex(v[k])
	count += 1

## Atalho: caixa alinhada aos eixos com rotação Y (graus) e centro.
func box(center: Vector3, size: Vector3, col: Color, rot_deg: float = 0.0, mat: String = "") -> void:
	add_box(Transform3D(Basis(Vector3.UP, deg_to_rad(rot_deg)), center), size, col, mat)

func add_cylinder(center: Vector3, radius: float, height: float, col: Color, segments: int = 8, mat: String = "", cap_bottom: bool = false) -> void:
	var st := _st(mat)
	var h := height * 0.5
	for i in segments:
		var a0 := TAU * i / segments
		var a1 := TAU * (i + 1) / segments
		var p0 := Vector3(cos(a0) * radius, 0, sin(a0) * radius)
		var p1 := Vector3(cos(a1) * radius, 0, sin(a1) * radius)
		var n0 := Vector3(cos(a0), 0, sin(a0))
		var n1 := Vector3(cos(a1), 0, sin(a1))
		var quad := [center + p0 + Vector3(0, -h, 0), center + p1 + Vector3(0, -h, 0), center + p1 + Vector3(0, h, 0), center + p0 + Vector3(0, h, 0)]
		var ns := [n0, n1, n1, n0]
		for tri in [[0, 1, 2], [0, 2, 3]]:
			for k in tri:
				st.set_color(col)
				st.set_normal(ns[k])
				st.add_vertex(quad[k])
		for v in [center + Vector3(0, h, 0), center + p0 + Vector3(0, h, 0), center + p1 + Vector3(0, h, 0)]:
			st.set_color(col)
			st.set_normal(Vector3.UP)
			st.add_vertex(v)
		if cap_bottom:
			for v in [center + Vector3(0, -h, 0), center + p1 + Vector3(0, -h, 0), center + p0 + Vector3(0, -h, 0)]:
				st.set_color(col)
				st.set_normal(Vector3.DOWN)
				st.add_vertex(v)
	count += 1

## Cilindro entre dois pontos quaisquer (canos, fios, galhos, pernas inclinadas).
func add_tube(a: Vector3, b: Vector3, radius: float, col: Color, segments: int = 6, mat: String = "") -> void:
	var st := _st(mat)
	var d := b - a
	var l := d.length()
	if l < 0.0001:
		return
	var y := d / l
	var x := y.cross(Vector3.UP if absf(y.y) < 0.95 else Vector3.RIGHT).normalized()
	var z := x.cross(y)
	for i in segments:
		var a0 := TAU * i / segments
		var a1 := TAU * (i + 1) / segments
		var n0 := x * cos(a0) + z * sin(a0)
		var n1 := x * cos(a1) + z * sin(a1)
		var q := [a + n0 * radius, a + n1 * radius, b + n1 * radius, b + n0 * radius]
		var ns := [n0, n1, n1, n0]
		for tri in [[0, 1, 2], [0, 2, 3]]:
			for k in tri:
				st.set_color(col)
				st.set_normal(ns[k])
				st.add_vertex(q[k])
	count += 1

func add_sphere(center: Vector3, radius: Vector3, col: Color, rings: int = 5, segments: int = 8, mat: String = "") -> void:
	var st := _st(mat)
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

## Quadrilátero livre (telhados, empenas). `facing` indica para onde a face deve olhar
## (a ordem dos pontos é invertida se preciso).
func add_quad(p: Array, col: Color, mat: String = "", facing: Vector3 = Vector3.ZERO) -> void:
	var st := _st(mat)
	var n: Vector3 = (p[1] - p[0]).cross(p[2] - p[0]).normalized()
	if facing != Vector3.ZERO and n.dot(facing) < 0.0:
		p = [p[3], p[2], p[1], p[0]]
		n = -n
	for tri in [[0, 2, 1], [0, 3, 2]]:
		for k in tri:
			st.set_color(col)
			st.set_normal(n)
			st.add_vertex(p[k])
	count += 1

func add_tri(p: Array, col: Color, mat: String = "", facing: Vector3 = Vector3.ZERO) -> void:
	var st := _st(mat)
	var n: Vector3 = (p[1] - p[0]).cross(p[2] - p[0]).normalized()
	if facing != Vector3.ZERO and n.dot(facing) < 0.0:
		p = [p[2], p[1], p[0]]
		n = -n
	for k in [0, 2, 1]:
		st.set_color(col)
		st.set_normal(n)
		st.add_vertex(p[k])
	count += 1

func _sph(theta: float, phi: float) -> Vector3:
	return Vector3(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi))

func build(roughness: float = 0.85, shadows: bool = true) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	if count == 0:
		return mi
	var mesh := ArrayMesh.new()
	var keys := surfaces.keys()
	keys.sort()
	for k in keys:
		surfaces[k].commit(mesh)
		var m: Material
		match k:
			"":
				m = Mat.vertex_color(roughness)
			"glass":
				m = Mat.glass(Color(0.55, 0.68, 0.76, 0.4))
			"water":
				m = Mat.water()
			_:
				m = Mat.named(k)
		mesh.surface_set_material(mesh.get_surface_count() - 1, m)
	mi.mesh = mesh
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON if shadows else GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	return mi
