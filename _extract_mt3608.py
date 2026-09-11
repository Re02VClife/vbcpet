import pypdf, sys, traceback
sys.stdout.reconfigure(encoding='utf-8')
try:
    r = pypdf.PdfReader(r"D:\vscode\vbcpet\_MT3608.pdf")
    print("pages:", len(r.pages))
    full = []
    for i, pg in enumerate(r.pages):
        t = pg.extract_text() or ""
        full.append("===PAGE %d===" % (i+1) + chr(10) + t)
    s = chr(10).join(full)
    open(r"D:\vscode\vbcpet\_MT3608.txt","w",encoding="utf-8").write(s)
    print("chars:", len(s))
except Exception:
    traceback.print_exc()
