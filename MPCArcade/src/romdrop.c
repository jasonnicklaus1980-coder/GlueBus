// MPC Arcade romdrop: a small web page for adding ROM zips from a phone or computer on the same network.
// Started by the library's ROM manager ("Web upload") and stopped when you leave it; never runs on its own.
//   romdrop --root /media/az01-internal/MAME --port 8080 --pin 1234
// Pages: GET /            upload page (PIN asked in the browser)
//        GET /api/list    installed files (JSON)                      header X-Pin required
//        PUT /api/upload?name=FILE.zip                                 header X-Pin required, body = the file
// Sets this MAME build knows go to roms/ (BIOS sets to bios/); other names go to import/, where the library's
// USB-import screen identifies zips by their contents. Only .zip and .7z are accepted. Files are written as .part
// and renamed when complete, so a broken upload never looks like a ROM set.
#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static char ROOT[400], PIN[16];
static volatile sig_atomic_t quit;
static void onSignal (int s) { (void) s; quit = 1; }

// known set names from system/gamedb.tsv (column 1 = name, column 17 = isbios)
static char (*setNames)[20]; static char* setBios; static int nSets;
static void loadSets (void)
{
    char p[500], line[1024];
    snprintf (p, sizeof p, "%s/system/gamedb.tsv", ROOT);
    FILE* f = fopen (p, "r");
    if (! f) return;
    int cap = 4096; setNames = malloc (sizeof *setNames * (size_t) cap); setBios = malloc ((size_t) cap);
    while (fgets (line, sizeof line, f))
    {
        if (nSets == cap) { cap *= 2; setNames = realloc (setNames, sizeof *setNames * (size_t) cap); setBios = realloc (setBios, (size_t) cap); }
        char* tab = strchr (line, '\t'); if (! tab) continue;
        *tab = 0; snprintf (setNames[nSets], 20, "%s", line);
        char* last = strrchr (tab + 1, '\t');
        setBios[nSets] = last && last[1] == '1';
        ++nSets;
    }
    fclose (f);
}
static int findSet (const char* n) { for (int i = 0; i < nSets; ++i) if (! strcmp (setNames[i], n)) return i; return -1; }

static void logmsg (const char* fmt, const char* a, const char* b)
{
    char t[32]; time_t tt = time (NULL); struct tm tm; localtime_r (&tt, &tm); strftime (t, sizeof t, "%H:%M:%S", &tm);
    printf ("%s ", t); printf (fmt, a, b); printf ("\n"); fflush (stdout);
}
static int sendAll (int fd, const void* buf, size_t n)
{
    const char* p = buf;
    while (n) { const ssize_t w = send (fd, p, n, MSG_NOSIGNAL); if (w <= 0) return -1; p += w; n -= (size_t) w; }
    return 0;
}
static void reply (int fd, int code, const char* type, const char* body)
{
    char h[300];
    const char* st = code == 200 ? "OK" : code == 400 ? "Bad Request" : code == 403 ? "Forbidden" : code == 404 ? "Not Found" : code == 413 ? "Too Large" : "Error";
    snprintf (h, sizeof h, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n", code, st, type, strlen (body));
    sendAll (fd, h, strlen (h)); sendAll (fd, body, strlen (body));
}

static const char* PAGE =
"<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>MPC Arcade upload</title><style>"
":root{--bg:#111113;--p:#1e1f23;--p2:#2a2b31;--t:#ececf0;--d:#9698a4;--a:#e8392f;--g:#3ecf6a;--b:#ef4a4a}"
"body{margin:0;background:var(--bg);color:var(--t);font:16px system-ui,sans-serif}"
"header{background:var(--p);padding:14px 20px;display:flex;gap:12px;align-items:center;border-bottom:1px solid #40424c}"
"header b{background:var(--a);border-radius:8px;padding:4px 10px}main{max-width:820px;margin:auto;padding:16px}"
"#drop{border:2px dashed #555;border-radius:14px;padding:40px 16px;text-align:center;color:var(--d);margin:16px 0}"
"#drop.on{border-color:var(--a);color:var(--t)}button,input{font:inherit}button{background:var(--a);color:#fff;border:0;border-radius:8px;padding:10px 18px}"
"input[type=password]{background:var(--p2);color:var(--t);border:1px solid #444;border-radius:8px;padding:9px;width:8em}"
".row{display:flex;justify-content:space-between;gap:10px;padding:8px 10px;border-bottom:1px solid #2a2b31}.row span:last-child{color:var(--d)}"
".ok{color:var(--g)}.bad{color:var(--b)}progress{width:100%}small{color:var(--d)}"
"</style></head><body><header><b>&#9654;</b><div><div>MPC Arcade</div><small>Add ROM zips to your MPC</small></div></header><main>"
"<div id=login><p>Enter the PIN shown on the MPC's screen (ROM manager &rarr; Web upload).</p>"
"<input id=pin type=password inputmode=numeric maxlength=8> <button onclick=go()>Connect</button> <span id=perr class=bad></span></div>"
"<div id=app hidden><div id=drop>Drop ROM zips here, or <label style='color:#fff;text-decoration:underline;cursor:pointer'>choose files"
"<input id=files type=file multiple accept='.zip,.7z' hidden></label><br><small>Use ROM sets you legally own (MAME 0.242 sets). "
"Unrecognised names go to MAME/import for identification on the MPC.</small></div><div id=queue></div>"
"<h3>On the MPC</h3><div id=list><small>loading&hellip;</small></div></div></main><script>"
"let P='';const $=i=>document.getElementById(i);"
"function api(u,o={}){o.headers=Object.assign({'X-Pin':P},o.headers||{});return fetch(u,o)}"
"async function go(){P=$('pin').value.trim();const r=await api('/api/list');if(r.status!=200){$('perr').textContent='Wrong PIN';return}"
"$('login').hidden=true;$('app').hidden=false;show(await r.json())}"
"function mb(n){return (n/1048576).toFixed(1)+' MB'}"
"function show(j){$('list').innerHTML=j.files.length?j.files.map(f=>`<div class=row><span>${f.dir}/${f.name}</span><span>${mb(f.size)}</span></div>`).join(''):'<small>No ROM files yet.</small>'}"
"async function refresh(){const r=await api('/api/list');if(r.status==200)show(await r.json())}"
"function up(file){return new Promise(res=>{const d=document.createElement('div');d.className='row';"
"d.innerHTML=`<span>${file.name}</span><span><progress max=100 value=0></progress></span>`;$('queue').prepend(d);"
"const x=new XMLHttpRequest();x.open('PUT','/api/upload?name='+encodeURIComponent(file.name));x.setRequestHeader('X-Pin',P);"
"x.upload.onprogress=e=>{if(e.lengthComputable)d.querySelector('progress').value=e.loaded*100/e.total};"
"x.onload=()=>{d.lastChild.innerHTML=x.status==200?`<span class=ok>${x.responseText}</span>`:`<span class=bad>${x.responseText||'failed'}</span>`;res()};"
"x.onerror=()=>{d.lastChild.innerHTML='<span class=bad>connection lost</span>';res()};x.send(file)})}"
"async function send(fs){for(const f of fs)await up(f);refresh()}"
"$('files').onchange=e=>send([...e.target.files]);const dr=$('drop');"
"dr.ondragover=e=>{e.preventDefault();dr.classList.add('on')};dr.ondragleave=()=>dr.classList.remove('on');"
"dr.ondrop=e=>{e.preventDefault();dr.classList.remove('on');send([...e.dataTransfer.files])};"
"$('pin').onkeydown=e=>{if(e.key=='Enter')go()};"
"</script></body></html>";

static int pinOk (const char* req)
{
    const char* h = strcasestr (req, "\r\nX-Pin:");
    if (! h) return 0;
    h += 8; while (*h == ' ') ++h;
    char got[16]; int n = 0;
    while (*h && *h != '\r' && n < 15) got[n++] = *h++;
    got[n] = 0;
    int diff = (int) strlen (got) ^ (int) strlen (PIN);
    for (size_t i = 0; i < strlen (PIN); ++i) diff |= got[i % (n ? n : 1)] ^ PIN[i];
    if (diff) { sleep (1); return 0; }                                   // slow down guessing
    return 1;
}
static void listJson (int fd)
{
    static char out[200000]; size_t k = 0;
    k += (size_t) snprintf (out + k, sizeof out - k, "{\"files\":[");
    const char* dirs[] = { "roms", "bios", "import" };
    int first = 1;
    for (int d = 0; d < 3; ++d)
    {
        char p[500]; snprintf (p, sizeof p, "%s/%s", ROOT, dirs[d]);
        DIR* dir = opendir (p); struct dirent* e;
        while (dir && (e = readdir (dir)) != NULL && k < sizeof out - 400)
        {
            const char* dot = strrchr (e->d_name, '.');
            if (e->d_name[0] == '.' || ! dot || (strcasecmp (dot, ".zip") && strcasecmp (dot, ".7z"))) continue;
            char f[800]; struct stat st; snprintf (f, sizeof f, "%s/%s", p, e->d_name);
            if (stat (f, &st)) continue;
            char name[300]; int n = 0;
            for (const char* c = e->d_name; *c && n < 290; ++c) { if (*c == '"' || *c == '\\') name[n++] = '\\'; name[n++] = *c; }
            name[n] = 0;
            k += (size_t) snprintf (out + k, sizeof out - k, "%s{\"dir\":\"%s\",\"name\":\"%s\",\"size\":%lld}", first ? "" : ",", dirs[d], name, (long long) st.st_size);
            first = 0;
        }
        if (dir) closedir (dir);
    }
    snprintf (out + k, sizeof out - k, "]}");
    reply (fd, 200, "application/json", out);
}
static int urldecode (const char* in, char* out, int n)
{
    int k = 0;
    for (; *in && *in != ' ' && *in != '&' && k < n - 1; ++in)
    {
        if (*in == '%' && isxdigit ((unsigned char) in[1]) && isxdigit ((unsigned char) in[2])) { char h[3] = { in[1], in[2], 0 }; out[k++] = (char) strtol (h, NULL, 16); in += 2; }
        else if (*in == '+') out[k++] = ' ';
        else out[k++] = *in;
    }
    out[k] = 0;
    return k;
}
static void upload (int fd, const char* req, const char* body, size_t have)
{
    const char* q = strstr (req, "name=");
    char name[256];
    if (! q || urldecode (q + 5, name, sizeof name) == 0) { reply (fd, 400, "text/plain", "no file name"); return; }
    // a plain file name only: no paths, no hidden files
    if (strchr (name, '/') || strchr (name, '\\') || name[0] == '.' || strstr (name, "..")) { reply (fd, 400, "text/plain", "bad file name"); return; }
    for (const char* c = name; *c; ++c) if ((unsigned char) *c < 32) { reply (fd, 400, "text/plain", "bad file name"); return; }
    const char* dot = strrchr (name, '.');
    if (! dot || (strcasecmp (dot, ".zip") && strcasecmp (dot, ".7z"))) { reply (fd, 400, "text/plain", "only .zip and .7z files"); return; }
    const char* cl = strcasestr (req, "\r\nContent-Length:");
    if (! cl) { reply (fd, 400, "text/plain", "no length"); return; }
    const long long len = atoll (cl + 17);
    if (len <= 0 || len > 4LL * 1024 * 1024 * 1024) { reply (fd, 413, "text/plain", "file too large"); return; }
    struct statvfs vs;
    if (statvfs (ROOT, &vs) == 0 && (long long) vs.f_bavail * (long long) vs.f_frsize < len + 50 * 1024 * 1024) { reply (fd, 413, "text/plain", "not enough free space on the MPC"); return; }
    char set[256]; snprintf (set, sizeof set, "%.*s", (int) (dot - name), name);
    for (char* c = set; *c; ++c) *c = (char) tolower ((unsigned char) *c);
    const int si = findSet (set);
    const char* dir = si < 0 ? "import" : (setBios[si] ? "bios" : "roms");
    char ext[8]; snprintf (ext, sizeof ext, "%s", dot);                // MAME looks for lower-case .zip / .7z
    for (char* c = ext; *c; ++c) *c = (char) tolower ((unsigned char) *c);
    char final[800], part[820];
    if (si >= 0) snprintf (final, sizeof final, "%s/%s/%s%s", ROOT, dir, set, ext);
    else snprintf (final, sizeof final, "%s/%s/%s", ROOT, dir, name);
    snprintf (part, sizeof part, "%s.part", final);
    const int out = open (part, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) { reply (fd, 500, "text/plain", "can't write on the MPC"); return; }
    long long done = 0;
    if (have > 0) { const size_t n = have > (size_t) len ? (size_t) len : have; if (write (out, body, n) != (ssize_t) n) { close (out); unlink (part); reply (fd, 500, "text/plain", "write failed"); return; } done = (long long) n; }
    static char buf[1 << 16];
    while (done < len && ! quit)
    {
        const ssize_t r = recv (fd, buf, sizeof buf, 0);
        if (r <= 0) break;
        if (write (out, buf, (size_t) r) != r) break;
        done += r;
    }
    close (out);
    if (done != len) { unlink (part); logmsg ("upload of %s failed%s", name, ""); return; }
    rename (part, final);
    logmsg ("received %s -> %s", name, final);
    char msg[300];
    if (si >= 0) snprintf (msg, sizeof msg, "saved to MAME/%s as %s%s", dir, set, ext);
    else snprintf (msg, sizeof msg, "unrecognised name: saved to MAME/import (identify it on the MPC: ROM manager > USB import)");
    reply (fd, 200, "text/plain", msg);
}
static void handle (int fd)
{
    struct timeval tv = { 30, 0 };
    setsockopt (fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    static char req[16384];
    size_t k = 0; char* end = NULL;
    while (k < sizeof req - 1)
    {
        const ssize_t r = recv (fd, req + k, sizeof req - 1 - k, 0);
        if (r <= 0) return;
        k += (size_t) r; req[k] = 0;
        if ((end = strstr (req, "\r\n\r\n")) != NULL) break;
    }
    if (! end) { reply (fd, 400, "text/plain", "bad request"); return; }
    const size_t headLen = (size_t) (end + 4 - req);
    if (! strncmp (req, "GET / ", 6) || ! strncmp (req, "GET /index.html ", 16)) { reply (fd, 200, "text/html; charset=utf-8", PAGE); return; }
    if (! strncmp (req, "GET /api/list", 13)) { if (pinOk (req)) listJson (fd); else reply (fd, 403, "text/plain", "wrong PIN"); return; }
    if (! strncmp (req, "PUT /api/upload", 15))
    {
        if (! pinOk (req)) { reply (fd, 403, "text/plain", "wrong PIN"); return; }
        upload (fd, req, req + headLen, k - headLen);
        return;
    }
    reply (fd, 404, "text/plain", "not found");
}
int main (int argc, char** argv)
{
    int port = 8080;
    for (int i = 1; i + 1 < argc; i += 2)
    {
        if (! strcmp (argv[i], "--root")) snprintf (ROOT, sizeof ROOT, "%s", argv[i + 1]);
        else if (! strcmp (argv[i], "--port")) port = atoi (argv[i + 1]);
        else if (! strcmp (argv[i], "--pin")) snprintf (PIN, sizeof PIN, "%s", argv[i + 1]);
    }
    if (! ROOT[0] || strlen (PIN) < 4) { fprintf (stderr, "usage: romdrop --root DIR --pin NNNN [--port 8080]\n"); return 1; }
    signal (SIGTERM, onSignal); signal (SIGINT, onSignal); signal (SIGPIPE, SIG_IGN);
    setvbuf (stdout, NULL, _IOLBF, 0);
    loadSets();
    const int s = socket (AF_INET, SOCK_STREAM, 0);
    const int one = 1; setsockopt (s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a; memset (&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons ((unsigned short) port); a.sin_addr.s_addr = htonl (INADDR_ANY);
    if (bind (s, (struct sockaddr*) &a, sizeof a) || listen (s, 8)) { perror ("romdrop: bind"); return 1; }
    printf ("romdrop: listening on port %d (%d known sets)\n", port, nSets);
    while (! quit)
    {
        const int c = accept (s, NULL, NULL);
        if (c < 0) { if (errno == EINTR) continue; break; }
        handle (c);
        close (c);
    }
    close (s);
    printf ("romdrop: stopped\n");
    return 0;
}
