import pypdf, sys, traceback
sys.stdout.reconfigure(encoding='utf-8')
p = r"D:\vscode\vbcpet\ESP32-P4C6-Core核心板_使用指南260413.pdf"
try:
    r = pypdf.PdfReader(p)
    print("pages:", len(r.pages))
    full = []
    for i, pg in enumerate(r.pages):
        try:
            t = pg.extract_text() or ""
        except Exception as e:
            t = "[ERR %s]" % e
        full.append("===PAGE %d===" % (i+1) + chr(10) + t)
    s = chr(10).join(full)
    open(r"D:\vscode\vbcpet\_pdf_text.txt","w",encoding="utf-8").write(s)
    print("chars:", len(s))
except Exception:
    traceback.print_exc()
