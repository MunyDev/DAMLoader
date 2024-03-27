build:
	sh generate_out.sh
	g++ main.cpp -Ixz-embedded/userspace -fpermissive -Ixz-embedded/linux/include/linux -Ixz-embedded/linux/include -o damloader ./out/damloader.pb.cc xz-embedded/linux/lib/decompress_unxz.c xz-embedded/linux/lib/xz/xz_dec_syms.c xz-embedded/linux/lib/xz/xz_dec_stream.c xz-embedded/linux/lib/xz/xz_dec_test.c xz-embedded/linux/lib/xz/xz_crc64.c xz-embedded/linux/lib/xz/xz_crc32.c xz-embedded/linux/lib/xz/xz_dec_lzma2.c xz-embedded/linux/lib/xz/xz_dec_bcj.c -lbz2 -lprotobuf
