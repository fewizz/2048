root=`dirname $(realpath ${BASH_SOURCE[0]})`

mkdir -p ${root}/build

compile_shader() {
	if ! glslangValidator \
		-e main \
		-o ${root}/build/$1.spv \
		-V ${root}/src/$1
	then
		exit 1
	fi
}

compile_shader tile.vert
compile_shader tile.frag

compile_shader digits_and_letters.vert
compile_shader digits_and_letters.frag

cp ${root}/src/digits_and_letters.png ${root}/build/

declare -a args

if [[ $OS != Windows_NT ]]; then
	#additional_args+=(-fsanitize=undefined)
	#additional_args+=(-fsanitize=memory)
	additional_args+=(-lglfw)
else
	additional_args+=(-static)
	additional_args+=(-lglfw3)
	additional_args+=(-lgdi32)
fi

clang++ \
	-std=c++2b \
	-nostdinc++ \
	-Wall \
	-Wextra \
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