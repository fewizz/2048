root=`dirname $(realpath ${BASH_SOURCE[0]})`

mkdir -p ${root}/build

glslangValidator \
	-e main \
	-o ${root}/build/tile.vert.spv \
	-V ${root}/src/tile.vert

glslangValidator \
	-e main \
	-o ${root}/build/tile.frag.spv \
	-V ${root}/src/tile.frag

cp ${root}/src/digits_and_letters.png ${root}/build/

declare -a args

if [[ $OS != Windows_NT ]]; then
	additional_args+=(-fsanitize=undefined)
	additional_args+=(-fsanitize=memory)
	additional_args+=(-lglfw)
else
	additional_args+=(-lglfw3)
	additional_args+=(-lgdi32)
fi

clang++ \
	-std=c++2b \
	-nostdinc++ \
	-Wall \
	-Wextra \
	-static \
	-g \
	-I ${root}/../core/include \
	-I ${root}/../math/include \
	-I ${root}/../posix-wrapper/include \
	-I ${root}/../windows-wrapper/include \
	-I ${root}/../vulkan-wrapper/include \
	-I ${root}/../glfw-wrapper/include \
	-I ${root}/../print/include \
	-o ${root}/build/2048 \
	${root}/src/main.cpp \
	-lpng \
	-lz \
	${additional_args[@]}