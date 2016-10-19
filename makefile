all: hello

hello: hello.cpp iridescent-map.cpp iridescent-map/cppo5m/o5m.cpp iridescent-map/cppo5m/varint.cpp iridescent-map/cppo5m/OsmData.cpp iridescent-map/cppGzip/DecodeGzip.cpp iridescent-map/TagPreprocessor.cpp iridescent-map/Regrouper.cpp iridescent-map/ReadInput.cpp iridescent-map/drawlib/drawlibcairo.cpp iridescent-map/drawlib/drawlib.cpp iridescent-map/drawlib/cairotwisted.cpp iridescent-map/drawlib/RdpSimplify.cpp iridescent-map/drawlib/LineLineIntersect.cpp iridescent-map/MapRender.cpp iridescent-map/Transform.cpp iridescent-map/Style.cpp iridescent-map/LabelEngine.cpp iridescent-map/TriTri2d.cpp iridescent-map/CompletePoly.cpp iridescent-map/Coast.cpp
	g++ `pkg-config --cflags gtk+-3.0` -o hello $^ `pkg-config --libs gtk+-3.0` -lz

