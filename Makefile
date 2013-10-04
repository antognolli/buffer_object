MYCFLAGS = ${shell pkg-config --cflags wayland-client ecore ecore-evas evas ecore-wayland elementary}
MYLDFLAGS = ${shell pkg-config --libs wayland-client ecore ecore-evas evas ecore-wayland elementary}

main: main.o buffer_object.o os-compatibility.o main video_layout.edj
	gcc ${MYCFLAGS} -g ${MYLDFLAGS} -o main main.o buffer_object.o os-compatibility.o

buffer_object.o: buffer_object.c
	gcc ${MYCFLAGS} -g -c buffer_object.c

main.o: main.c
	gcc ${MYCFLAGS} -g -c main.c

video_layout.edj: video_layout.edc
	edje_cc video_layout.edc

clean:
	rm main.o buffer_object.o os-compatibility.o main video_layout.edj
