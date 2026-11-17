# archive

The reader of zip archives: the firmware package now, the albums given as zips later. Qt Core only, no other library. The rules are in `docs/specs/install-wizard.md` section 7.6.

- `ZipReader`: reads the end record (Zip64 included), the central directory and each local header, and refuses the whole archive when an entry is encrypted, packed with another method than stored or deflate, named so that it could land outside its folder, a link, or when the archive is over the limits the caller gives. It unpacks an entry by pieces, checking its size and its CRC-32. Names are UTF-8 when the archive says so or when they read as UTF-8, else Latin-1.
- `inflate()`: our decoder of deflate data, written from RFC 1951: stored blocks, and blocks with the fixed or their own Huffman codes. Each code is looked up in a table with one cell for every value of its longest code.
- `Crc32`: the CRC-32 of the zip format.
