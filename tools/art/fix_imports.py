"""Ajusta as opções de importação das texturas do jogo no Godot (rodar após gerar texturas
novas e um primeiro `godot --import`): compressão de VRAM e mipmaps para 3D; normais
marcadas como normal map; a máscara de regiões da pele fica sem perdas e sem mipmaps."""
import glob
import re


def setp(s, k, v):
    return re.sub(r"^%s=.*$" % re.escape(k), "%s=%s" % (k, v), s, flags=re.M)


for f in glob.glob("assets/textures/*.png.import") + glob.glob("assets/characters/*.png.import"):
    s = open(f).read()
    if "skin_mask" in f:
        s = setp(s, "compress/mode", "0")
        s = setp(s, "mipmaps/generate", "false")
    else:
        s = setp(s, "compress/mode", "2")
        s = setp(s, "mipmaps/generate", "true")
        s = setp(s, "compress/normal_map", "1" if f.endswith("_normal.png.import") else "0")
    s = setp(s, "detect_3d/compress_to", "0")
    open(f, "w").write(s)
