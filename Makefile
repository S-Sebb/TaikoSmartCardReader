all:
	@meson compile -C build
	@strip build/scardreader.dll

setup:
	@meson setup build --cross cross-mingw-64.txt

dist-no-7z: all
	@mkdir -p out/
	@cp build/scardreader.dll out/

dist: dist-no-7z
	@cd out && 7z a -t7z ../dist.7z .
	@rm -rf out
