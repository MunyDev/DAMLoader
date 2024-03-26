build:
	sh generate_out.sh
	g++ main.cpp -o damloader ./out/damloader.pb.cc -lprotobuf