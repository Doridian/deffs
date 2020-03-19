deffs (Defaulting FS)

This filesystem will proxy through to an underlying filesystem directory (like overlayfs and others).
However, if a file is trying to be accessed that is not found, deffs will instead present the given default file (via `-odeffile=/a/file`)

Usage examples
- You want to run an nginx with lots of certs, some of which do not always exist (think ACME/LetsEncrypt), so a dummy certificate can be presented allowing nginx to start

```
gcc -O2 -D_FILE_OFFSET_BITS=64 main.c -lfuse -o /usr/bin/deffs
./deffs -d -f -s -odeffile=default_file ./src ./dest
mount -t deffs ....
```
