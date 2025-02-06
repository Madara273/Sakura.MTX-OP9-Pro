#!/bin/bash
# build.sh - Android Kernel Build Script to k5.x
# Make sure clang is added to your path before using this script
# Semi-automatic script suitable for use in Ubuntu, Debian, Kali and NetHunter
# Author Madara273
# ----------------------------------------------------------------------------

# ---- Define colors (real ESC via $'...') ----
GREEN=$'\033[0;32m'
RED=$'\033[0;31m'
YELLOW=$'\033[1;33m'
PURPLE=$'\033[0;35m'
MAGENTA=$'\033[1;35m'
LGREEN=$'\033[92m'
PINK=$'\033[38;5;206m'
CYAN=$'\033[0;36m'
BLUE=$'\033[0;34m'
GREY=$'\033[38;5;250m'   # light grey (256-color)
NC=$'\033[0m'            # reset

# ---- Get the absolute path of the current directory ----
CURRENT_DIR="$(pwd)"

# ---- Set Eastern Time timezone ----
export TZ=Europe/Kiev # Enter your time zone

# ---- random_color - generates a random color for output to the terminal ----
random_color() {
	local colors=($GREEN $RED $YELLOW $PURPLE $BLUE)	# Array of colors
	local random_index=$((RANDOM % ${#colors[@]}))		# Random index
	echo -e "${colors[$random_index]}"			# For color interpretation
}

# ---- Get information about the distribution and its version ----
. /etc/os-release 2>/dev/null || { OS=$(uname -s); VERSION_ID=$(uname -r); }

# ---- Output information to the terminal -----
echo -e "\n$(random_color)━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo -e "    - OS: $NAME $VERSION_ID"
echo -e "    - Kernel: $(uname -r)"
echo -e "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}\n"

# ----  ASCII Art Logo with random colors ----
ascii_art_logo() {
	echo -e "
$(random_color)********************************SAKURA********************************${NC}
$(random_color) ______ ______ _______ _______ _______ _______ ___ ___ ______ _______ ${NC}
$(random_color)|   __ \   __ \       |_     _|       |_     _|   |   |   __ \    ___|${NC}
$(random_color)|    __/      <   -   | |   | |   -   | |   |  \     /|    __/    ___|${NC}
$(random_color)|___|  |___|__|_______| |___| |_______| |___|   |___| |___|  |_______|${NC}
"
}

# ---- Prompt user for input ----
echo -e "${PURPLE}Enter KBUILD_USER:${NC}"
read -t 5 -rp "KBUILD_USER: " KBUILD_USER
KBUILD_USER="${KBUILD_USER:-Madara273}"	# If user doesn't enter a value, use "Madara273"
echo "$KBUILD_USER"	# Output the value of KBUILD_USER

echo -e "${PURPLE}Enter KBUILD_HOST:${NC}"
read -t 5 -rp "KBUILD_HOST: " KBUILD_HOST
KBUILD_HOST="${KBUILD_HOST:-Kali_GNU/Linux-2026.2}"	# If user doesn't enter a value, use "Kali_GNU/Linux-2026.2"
echo "$KBUILD_HOST"	# Output the value of KBUILD_HOST

# ---- Prompt for Compiler ----
echo -e "\n${YELLOW}Choose your weapon for compilation:${NC}"
echo -e "${PINK}1.👉 BEST EVA GCC (Bleeding Edge)${NC}"
echo -e "${GREEN}2.👉 SAKURA CLANG (God Mode)${NC}"
read -t 5 -rp "Compiler [1]: " COMPILER_CHOICE
COMPILER_CHOICE="${COMPILER_CHOICE:-1}"

# ---- Set environment variables and Target Variables ----
THREAD="${1:-$(nproc --all)}"
TARGET_ARCH="arm64"
TARGET_SUBARCH="arm64"
TARGET_CROSS_COMPILE_COMPAT="arm-linux-gnueabi-"
TARGET_BUILD_USER="$KBUILD_USER"
TARGET_BUILD_HOST="$KBUILD_HOST"
TARGET_DEVICE="lahaina-qgki"
TARGET_PRODUCT="$TARGET_DEVICE"
TARGET_OUT="$(pwd)/../SAKURA_OUT"
TARGET_DTC_FLAGS="-q"

[ "$COMPILER_CHOICE" == "1" ] && {
	echo -e "${PINK}Switching to Eva GCC... Let's make it bleed~${NC}"
	export PATH="$(pwd)/../Madara-Built/EVA/bin:$PATH"
	export CROSS_COMPILE="aarch64-elf-"
	export CROSS_COMPILE_ARM32="arm-linux-gnueabi-"
	TARGET_CC="aarch64-elf-gcc"
	TARGET_HOSTLD="aarch64-elf-ld"
	TARGET_CROSS_COMPILE="aarch64-elf-"
	TARGET_CLANG_TRIPLE=""
	CC_ADDITIONAL_FLAGS="-Wno-error=unused-function CROSS_COMPILE_COMPAT=arm-linux-gnueabi- GCC_LTO=1 GRAPHITE=1"
	} || {
	echo -e "${GREEN}Sakura Clang engaged. Prepare for speed.${NC}"
	export PATH="$(pwd)/../Madara-Built/SAKURA/bin:$PATH"
	export CLANG_TRIPLE="aarch64-linux-gnu-"
	export CROSS_COMPILE="aarch64-linux-gnu-"
	export CROSS_COMPILE_ARM32="arm-linux-gnueabi-"
	TARGET_CC="clang"
	TARGET_HOSTLD="ld.lld"
	TARGET_CROSS_COMPILE="aarch64-linux-gnu-"
	TARGET_CLANG_TRIPLE="aarch64-linux-gnu-"
	CC_ADDITIONAL_FLAGS="LLVM_IAS=1 LLVM=1 -Wno-error=unused-function"
}

TARGET_COMPILER_STRING="$COMPILER_STRING"
TARGET_LD_VERSION="$LD_VERSION"
TARGET_CC_VERSION="$CC_VERSION"

# ---- Kernel target parameters ----
TARGET_KERNEL_FILE="$TARGET_OUT/arch/arm64/boot/Image"
TARGET_KERNEL_DTB="$TARGET_OUT/arch/arm64/boot/dtb"
TARGET_KERNEL_DTB_IMG="$TARGET_OUT/arch/arm64/boot/dtb.img"
TARGET_KERNEL_DTBO_IMG="$TARGET_OUT/arch/arm64/boot/dtbo.img"
TARGET_KERNEL_NAME="Kernel"
get_kernel_version(){
	TARGET_KERNEL_MOD_VERSION="$(make kernelversion O=$TARGET_OUT ARCH=arm64)"
}

# ---- Final kernel build parameters ----
FINAL_KERNEL_BUILD_PARA="ARCH=$TARGET_ARCH \
			 SUBARCH=$TARGET_SUBARCH \
			 HOSTLD=$TARGET_HOSTLD \
			 CC=$TARGET_CC \
			 CROSS_COMPILE=$TARGET_CROSS_COMPILE \
			 CROSS_COMPILE_COMPAT=$TARGET_CROSS_COMPILE_COMPAT \
			 $CC_ADDITIONAL_FLAGS \
			 DTC_FLAGS=$TARGET_DTC_FLAGS \
			 O=$TARGET_OUT \
			 CC_VERSION=$TARGET_CC_VERSION \
			 LD_VERSION=$TARGET_LD_VERSION \
			 TARGET_PRODUCT=$TARGET_DEVICE \
			 KBUILD_COMPILER_STRING=$TARGET_COMPILER_STRING \
			 KBUILD_BUILD_USER=$TARGET_BUILD_USER \
			 KBUILD_BUILD_HOST=$TARGET_BUILD_HOST \
			 -j$THREAD"

# CLANG_TRIPLE Only Clang.
[ -n "$TARGET_CLANG_TRIPLE" ] && FINAL_KERNEL_BUILD_PARA="$FINAL_KERNEL_BUILD_PARA CLANG_TRIPLE=$TARGET_CLANG_TRIPLE"

# ----  Defconfig parameters ----
DEFCONFIG_PATH=arch/arm64/configs
DEFCONFIG_NAME="sakura_defconfig"

# ---- Time parameters ----
START_SEC=$(date +%s)
CURRENT_TIME=$(date '+%Y%m%d-%H%M')

# ---- Setup secure keystore paths ----
HOME="${HOME:-/tmp}"
[ "$HOME" = "/" ] && HOME="/tmp"
SIGNER_DIR="$HOME/.sakura"

KEYSTORE="$SIGNER_DIR/keystore.p12"
PASSFILE="$SIGNER_DIR/.store_pass"
ALIASFILE="$SIGNER_DIR/.alias"
ROOTCA_KEY="$SIGNER_DIR/rootCA.key"
ROOTCA_CERT="$SIGNER_DIR/rootCA.pem"
TENZO_KEY="$SIGNER_DIR/tenzo.key"
TENZO_CERT="$SIGNER_DIR/tenzo.crt"
P12_FILE="$SIGNER_DIR/tenzo.p12"
CSR_FILE="$SIGNER_DIR/tenzo.csr"

# ---- Logging / Output ----
AK3_PATH="$TARGET_OUT/AnyKernel3"
LOG_FILE="$AK3_PATH/build.log"
WARNING_PATTERN="warning"
ERROR_PATTERN="error"
NORMAL_PATTERN="normal"

# ---- Getting information about git remote, branch and commit ----
remote=$(git remote -v 2>&1 | grep push | head -n1 | cut -f2 | sed "s/(push)//" | cut -f4 -d "/")
domain=$(git remote -v 2>&1 | grep push | head -n1 | cut -f2 | sed "s/(push)//" | cut -f5 -d "/" | xargs)
branch=$(git status 2>&1 | grep "On branch" | sed -e 's/On branch //g')
commit=$(git rev-parse --short=8 HEAD)

# ----  Kernel DIR ----
KERNEL_DIR=$(pwd)
echo -e "${GREEN}$KERNEL_DIR${NC}"

# ---- Function to display build information ----
display_build_info(){
	echo -e "${PURPLE}***************Sakura-Kernel**************${NC}"
	echo -e "PRODUCT: $TARGET_DEVICE"
	echo -e "USER: $KBUILD_USER"
	echo -e "HOST: $KBUILD_HOST"
	echo -e "SUBLEVEL: $(grep -E '^SUBLEVEL =' Makefile | awk '{print $3}')"
	echo -e "${PURPLE}***************Device-Builder**************${NC}"
	echo -e "BUILD_DEVICE: $(lsb_release -a 2>/dev/null | grep Description | cut -d: -f2 | xargs)"
	echo -e "Compiler: $($TARGET_CC --version | head -n 1)"
	echo -e "Core count: $(nproc)"
	echo -e "Build Date: $(date +"%Y-%m-%d %H:%M")"
	echo -e "${PURPLE}*************last commit details***********${NC}"
	echo -e "Last commit (name): $(git log -1 --pretty=format:%s)"
	echo -e "Last commit (hash): $(git log -1 --pretty=format:%H)"
	echo -e "${PURPLE}*******************************************${NC}"
}

# ---- Function for interactive action selection with timeout ----
choose_action(){
	while true; do
		echo -e "Choose an action:"
		echo -e "${GREEN}1.👉 Install necessary packages${NC}"
		echo -e "${GREEN}2.👉 Start kernel compilation${NC}"
		echo -e "${GREEN}3.👉 Exit program${NC}"

		# Set timeout for user input (5 seconds)
		read -t 5 -p "Enter the action number (1/2/3): " choice

		# If no input is provided within 5 seconds, default to action 1 and then 2
		[ -z "$choice" ] && echo -e "${YELLOW}No input detected. Automatically selecting action 1.${NC}" && install_packages && echo -e "${YELLOW}Proceeding to action 2 automatically.${NC}" && compile_kernel && break

		case $choice in
			1 ) install_packages;;
			2 ) compile_kernel;;
			3 ) exit;;
			* ) echo -e "${RED}Invalid choice. Please enter 1, 2, or 3.${NC}";;
		esac
	done
}

# Function for "smart" installation
pkg_install() {
	[ -f /etc/arch-release ] && [ -n "$1" ] && sudo pacman -S --needed --noconfirm "$1"
	[ ! -f /etc/arch-release ] && [ -n "$2" ] && sudo apt-get install -y "$2"
}

# ---- Install packages ----
install_packages(){
	echo -e "${YELLOW}Starting package installation...${NC}"

	# pkg_install "Name Arch" "Name Debian"
	pkg_install "bc" "bc"
	pkg_install "bison" "bison"
	pkg_install "base-devel" "build-essential"
	pkg_install "zstd" "zstd"
	pkg_install "clang" "clang"
	pkg_install "lld" "lld"
	pkg_install "flex" "flex"
	pkg_install "gnupg" "gnupg"
	pkg_install "gperf" "gperf"
	pkg_install "ccache" "ccache"
	pkg_install "lz4" "liblz4-tool"
	pkg_install "sdl12-compat" "libsdl1.2-dev"
	pkg_install "libxml2" "libxml2"
	pkg_install "" "libxml2-utils"
	pkg_install "libpng" "pngcrush"
	pkg_install "schedtool" "schedtool"
	pkg_install "squashfs-tools" "squashfs-tools"
	pkg_install "libxslt" "xsltproc"
	pkg_install "zlib" "zlib1g-dev"
	pkg_install "ncurses" "libncurses5-dev"
	pkg_install "bzip2" "bzip2"
	pkg_install "git" "git"
	pkg_install "gcc" "gcc"
	pkg_install "gcc" "g++"
	pkg_install "openssl" "libssl-dev"
	pkg_install "openssl" "openssl"
	pkg_install "aarch64-linux-gnu-gcc" "gcc-aarch64-linux-gnu"
	pkg_install "gcc-arm-linux-gnueabi" "gcc-arm-linux-gnueabi"
	pkg_install "llvm" "llvm"
	pkg_install "python-pip" "python3-pip"
	pkg_install "cpio" "cpio"
	pkg_install "binutils" "binutils"
	pkg_install "zip" "zip"
	pkg_install "dtc" "device-tree-compiler"
	pkg_install "jdk21-openjdk" "default-jre"
	pkg_install "jdk21-openjdk" "openjdk-21-jdk"

	echo -e "${GREEN}Necessary packages successfully installed.${NC}"
}

# ---- Clone Anykernel3 ----
clone_anykernel3(){
	while true; do
		echo -e "${YELLOW}Select branch to clone:${NC}"
		echo -e "${BLUE}1.👉 Sakura_OP9Pro-built-in${NC}"
		echo -e "${BLUE}2.👉 Sakura_OP9Pro-modules${NC}"
		echo -e "${BLUE}3.👉 Custom git clone command${NC}"

		# Set timeout for user input (5 seconds)
		read -t 5 -rp "Enter your choice (1, 2 or 3): " choice

		# If no input is provided within 5 seconds, default to action 1 (Sakura_OP9)
		[ -z "$choice" ] && echo -e "${YELLOW}No input detected. Automatically selecting Sakura_OP9.${NC}" && choice=1

		case $choice in
			1)
				branch="Sakura_OP9"
				git clone --depth=1 https://github.com/Madara273/AnyKernel3.git -b "$branch" "$AK3_PATH" &&
				{ echo -e "${GREEN}Clone successful.${NC}"; break; } || echo -e "${RED}Clone failed.${NC}"
				;;
			2)
				branch="Sakura_OP9-M"
				git clone --depth=1 https://github.com/Madara273/AnyKernel3.git -b "$branch" "$AK3_PATH" &&
				{ echo -e "${GREEN}Clone successful.${NC}"; break; } || echo -e "${RED}Clone failed.${NC}"
				;;
			3)
				while true; do
					read -rp "Enter the full git clone command (e.g., git clone https://github.com/username/repository.git -b branch_name): " clone_command
					# Execute the custom command and check its success
					eval "$clone_command" && { echo -e "${GREEN}Clone successful.${NC}"; break; } ||
					echo -e "${RED}Clone failed. Please try again.${NC}"
				done
				return 0
				;;
			*)
				echo -e "${RED}Invalid choice. Please try again.${NC}"
				;;
		esac
	done
}

# ---- Function to check for necessary tools ----
check_tools(){
	echo -e "${YELLOW}Checking for necessary tools...${NC}"

	[ "$COMPILER_CHOICE" == "1" ] && {
		command -v aarch64-elf-gcc >/dev/null 2>&1 || { echo -e "${RED}Eva GCC is not installed or not in PATH!${NC}"; exit 1; }
		command -v arm-linux-gnueabi-gcc >/dev/null 2>&1 || { echo -e "${RED}32-bit GCC is not installed!${NC}"; exit 1; }
	}

	[ "$COMPILER_CHOICE" != "1" ] && {
		command -v clang >/dev/null 2>&1 || { echo -e "${RED}clang is not installed.${NC}"; exit 1; }
	}
	for tool in make mke2fs dtc; do
		command -v "$tool" >/dev/null 2>&1 || { echo -e "${RED}$tool is not installed.${NC}"; exit 1; }
	done

	echo -e "${GREEN}All necessary tools are installed.${NC}"
}

# ---- Function to create default kernel configuration ----
make_defconfig(){
	echo -e "${YELLOW}------------------------------${NC}"
	echo -e "${YELLOW} Generating kernel configuration...${NC}"
	make $FINAL_KERNEL_BUILD_PARA $DEFCONFIG_NAME || { echo -e "${RED}Failed to create default kernel configuration.${NC}"; exit 1; }
	echo -e "${GREEN}Default kernel configuration created successfully.${NC}"
	echo -e "${YELLOW}------------------------------${NC}"
}

# ---- Function for building a kernel with color output and countdown ----
build_kernel() {
	echo -e "${YELLOW}---------------------------------------${NC}"
	echo -e "${YELLOW} Building the kernel...${NC}"
	echo -e "${YELLOW}---------------------------------------${NC}"

	set -e		# Exit on error
	set -o pipefail	# Catch errors or Ctrl+C inside the make | while pipeline

	# ---- Colored logger (stdout + log file) ----
	log_echo() {
		local color="$1"
		shift
		local message="$*"

		echo -e "${color}${message}${NC}"
		echo -e "${color}${message}${NC}" >> "$LOG_FILE"
	}

	# ---- Time tracking ----
	START_SEC=$(date +%s)

	TIME_COLOR="$BLUE"

	declare -A COLORS=(
		[normal]="$GREEN"
		[warning]="$PURPLE"
		[error]="$RED"
	)

	# ---- Kernel build with realtime log parsing ----
	make $FINAL_KERNEL_BUILD_PARA 2>&1 | while IFS= read -r line; do

		CURRENT_SEC=$(date +%s)
		ELAPSED_SEC=$(( CURRENT_SEC - START_SEC ))

		TIME_FMT=$(printf "%02d:%02d" $(( ELAPSED_SEC / 60 )) $(( ELAPSED_SEC % 60 )))

		level="normal"
		[[ $line =~ $WARNING_PATTERN ]] && level="warning"
		[[ $line =~ $ERROR_PATTERN   ]] && level="error"

		log_echo "${TIME_COLOR}${TIME_FMT} ${COLORS[$level]}" "$line"

	done

	# ---- Module installation ----
	MODULES_DIR="$TARGET_OUT/modules_inst"
	mkdir -p "$MODULES_DIR"

	GREP_Y=$(grep -rn '^CONFIG_MODULES=y$' "$AK3_PATH/defconfig") && {
		echo -e "${GREEN}${GREP_Y}${NC}"
		echo -e "${YELLOW}---------------------------------------${NC}"
		echo -e "${YELLOW} Installing kernel modules...${NC}"
		echo -e "${YELLOW}---------------------------------------${NC}"

		make $FINAL_KERNEL_BUILD_PARA INSTALL_MOD_PATH="$MODULES_DIR" INSTALL_MOD_STRIP=1 modules_install

		echo -e "${GREEN}Kernel modules ready (external .ko files available)${NC}"
	} || {
		GREP_NOT_SET=$(grep -rn '^# CONFIG_MODULES is not set$' "$AK3_PATH/defconfig") && {
			echo -e "${PURPLE}${GREP_NOT_SET}${NC}"
			echo -e "${PURPLE}# CONFIG_MODULES is not set — all features built-in to kernel${NC}"
		} || {
			echo -e "${RED}Unexpected CONFIG_MODULES value — check $AK3_PATH/defconfig!${NC}"
		}
	}

	# ---- Total build time ----
	END_SEC=$(date +%s)
	TOTAL_SEC=$(( END_SEC - START_SEC ))

	echo -e "${GREEN}Kernel build took $(( TOTAL_SEC / 60 ))m $(( TOTAL_SEC % 60 ))s${NC}" \
		| tee -a "$LOG_FILE"
}

# ---- Function to link all dtb files -----
link_all_dtb_files(){
	echo -e "${YELLOW}Linking all dtb and dtbo files...${NC}"

	# Ensure the output directories exist
	mkdir -p $TARGET_OUT/arch/arm64/boot

	# Link .dtb files
	echo -e "${YELLOW}Linking .dtb files...${NC}"
	find $TARGET_OUT/arch/arm64/boot/dts/ -name '*.dtb' -exec cat {} + > $TARGET_OUT/arch/arm64/boot/dtb ||
	echo -e "${RED}Failed to link .dtb files.${NC}"

	echo -e "${YELLOW}Linking completed.${NC}"
}

# ---- Generate a secure keystore with a trusted cert for ZIP/APK signing ----
generate_secure_keystore() {
	[[ -f "$KEYSTORE" ]] && echo -e "${GREEN}✔ Keystore already exists: $KEYSTORE${NC}" && return

	echo -e "${YELLOW}Creating new keystore and full trust chain...${NC}"

	rm -rf "$SIGNER_DIR"
	mkdir -p "$SIGNER_DIR" && chmod 700 "$SIGNER_DIR"

	PASS=$(tr -dc A-Za-z0-9 </dev/urandom | head -c 18)
	UNIQUE_ID=$(date +%Y%m%d_%H%M%S)
	KEYALIAS="tenzoKey_${UNIQUE_ID}"

	echo "$PASS" > "$PASSFILE"
	echo "$KEYALIAS" > "$ALIASFILE"
	chmod 600 "$PASSFILE" "$ALIASFILE"

	echo -e "${CYAN}Generating root CA...${NC}" &&
	openssl genrsa -out "$ROOTCA_KEY" 4096 &&
	openssl req -x509 -new -nodes -key "$ROOTCA_KEY" -sha256 -days 9125 \
		-out "$ROOTCA_CERT" -subj "/CN=Tenzo Root CA/O=Madara273/C=UA" ||
	{ echo -e "${RED}✘ Root CA generation failed${NC}"; return 1; }

	echo -e "${CYAN}Generating Tenzo keypair and CSR...${NC}" &&
	openssl genrsa -out "$TENZO_KEY" 4096 &&
	openssl req -new -key "$TENZO_KEY" -out "$CSR_FILE" \
		-subj "/CN=Tenzo, OU=Root, O=Madara273, L=UA, ST=Ukraine, C=UA" ||
	{ echo -e "${RED}✘ Keypair or CSR failed${NC}"; return 1; }

	openssl x509 -req -in "$CSR_FILE" -CA "$ROOTCA_CERT" -CAkey "$ROOTCA_KEY" \
		-CAcreateserial -out "$TENZO_CERT" -sha256 -days 365 || \
	{ echo -e "${RED}✘ Signing failed${NC}"; return 1; }

	echo -e "${CYAN}Exporting to PKCS#12...${NC}" &&
	openssl pkcs12 -export \
		-inkey "$TENZO_KEY" \
		-in "$TENZO_CERT" \
		-certfile "$ROOTCA_CERT" \
		-out "$P12_FILE" \
		-password pass:"$PASS" \
		-name "$KEYALIAS" ||
	{ echo -e "${RED}✘ PKCS#12 export failed${NC}"; return 1; }

	keytool -importkeystore \
		-srckeystore "$P12_FILE" -srcstoretype PKCS12 -srcstorepass "$PASS" \
		-destkeystore "$KEYSTORE" -deststoretype PKCS12 -deststorepass "$PASS" \
		-alias "$KEYALIAS" -noprompt || \
	{ echo -e "${RED}✘ PKCS12 import failed${NC}"; return 1; }

	keytool -importcert -trustcacerts -alias "rootCA_${UNIQUE_ID}" -file "$ROOTCA_CERT" \
		-keystore "$KEYSTORE" -storepass "$PASS" -noprompt ||
	{ echo -e "${RED}✘ Failed to add rootCA to keystore${NC}"; return 1; }

	# Java truststore
	echo -e "${CYAN}Adding Root CA to Java truststore...${NC}"

	sudo keytool -list -cacerts -storepass changeit -alias "tenzoRoot" >/dev/null 2>&1 && \
	sudo keytool -delete -cacerts -storepass changeit -alias "tenzoRoot" -noprompt

	sudo keytool -importcert -alias "tenzoRoot" -file "$ROOTCA_CERT" \
		-cacerts -storepass changeit -noprompt && \
		echo -e "${GREEN}✔ Added to Java truststore (cacerts)${NC}" || \
		echo -e "${RED}✘ Failed to add to Java truststore${NC}"

	echo -e "${GREEN}✔ Keystore created: $KEYSTORE${NC}"
	show_fingerprint
}

# ----  SIGN ZIP FILE ----
sign_zip() {
	ZIP="$1"
	OUT="$2"

	[[ -f "$ZIP" ]] || {
		echo -e "${RED}✘ ZIP file not found: $ZIP${NC}"
		exit 1
	}

	generate_secure_keystore

	PASS=$(cat "$PASSFILE")
	KEYALIAS=$(cat "$ALIASFILE")

	cp "$ZIP" "$OUT"

	echo -e "${CYAN}Signing ZIP with timestamp...${NC}"
	SIGNING_OUTPUT=$(jarsigner -keystore "$KEYSTORE" -storepass "$PASS" -keypass "$PASS" \
		-sigalg SHA256withRSA -digestalg SHA-256 \
		-tsa http://timestamp.digicert.com \
		"$OUT" "$KEYALIAS" 2>&1)

	echo "$SIGNING_OUTPUT" | grep -q "jar signed." &&
	{
		echo -e "${GREEN}✔ Signed successfully: $OUT${NC}"
		echo "$SIGNING_OUTPUT" | grep -q "The timestamp will expire" &&
			echo -e "${YELLOW}$(echo "$SIGNING_OUTPUT" | grep "The timestamp will expire")${NC}"
	} || {
		echo -e "${RED}❌ Signing failed!${NC}"
		echo "$SIGNING_OUTPUT"
		exit 1
	}
}

# ---- SHOW FINGERPRINT ----
show_fingerprint() {
	[[ -f "$KEYSTORE" && -f "$PASSFILE" && -f "$ALIASFILE" ]] &&
	{
		PASS=$(cat "$PASSFILE")
		KEYALIAS=$(cat "$ALIASFILE")
		echo -e "${CYAN}SHA256 Fingerprint:${NC}"
		keytool -list -v -keystore "$KEYSTORE" -storepass "$PASS" -alias "$KEYALIAS" 2>/dev/null | grep "SHA256:"
	} || {
		echo -e "${RED}✘ Cannot display fingerprint. Keystore or data missing.${NC}"
	}
}

# ----  Generate flashable archive and sign it (with modules) ----
generate_flashable() {
	echo -e "${YELLOW}------------------------------ ${NC}"
	echo -e "${YELLOW} Generating flashable kernel ${NC}"
	echo -e "${YELLOW}------------------------------ ${NC}"

	AK3_PATH="$TARGET_OUT/AnyKernel3"
	ANYKERNEL_PATH="AnyKernel3"

	echo -e "${YELLOW} Fetching AnyKernel ${NC}"

	cd "$TARGET_OUT" || return 1

	# ---------------- Kernel files ----------------
	echo -e "${YELLOW} Copying kernel files ${NC}"

	cp -r "$TARGET_KERNEL_FILE"     "$ANYKERNEL_PATH/" || echo -e "${RED}Failed to copy TARGET_KERNEL_FILE${NC}"
	cp -r "$TARGET_KERNEL_DTB"      "$ANYKERNEL_PATH/" || echo -e "${RED}Failed to copy TARGET_KERNEL_DTB${NC}"
	cp -r "$TARGET_KERNEL_DTB_IMG"  "$ANYKERNEL_PATH/" || echo -e "${RED}Failed to copy TARGET_KERNEL_DTB_IMG${NC}"
	cp -r "$TARGET_KERNEL_DTBO_IMG" "$ANYKERNEL_PATH/" || echo -e "${RED}Failed to copy TARGET_KERNEL_DTBO_IMG${NC}"

	# Module logic executes ONLY if CONFIG_MODULES=y
	grep -q '^CONFIG_MODULES=y$' "$ANYKERNEL_PATH/defconfig" 2>/dev/null && {

		# ----------- VENDOR_RAMDISK MODULES LOGIC -----------
		echo -e "${YELLOW} Copying 6 specific modules to vendor_ramdisk ${NC}"

		RAMDISK_MODULE_DIR="$ANYKERNEL_PATH/vendor_ramdisk/lib/modules"
		mkdir -p "$RAMDISK_MODULE_DIR"

		SPECIFIC_MODULES=(
			"adsp_loader_dlkm.ko"
			"apr_dlkm.ko"
			"msm_drm.ko"
			"q6_notifier_dlkm.ko"
			"q6_pdr_dlkm.ko"
			"snd_event_dlkm.ko"
		)

		for mod in "${SPECIFIC_MODULES[@]}"; do
			mod_path=$(find "$TARGET_OUT/modules_inst" -type f -name "$mod" -print -quit)
			[ -f "$mod_path" ] && {
				cp "$mod_path" "$RAMDISK_MODULE_DIR/"
				echo -e "${GREEN}  + Added to vendor_ramdisk: $mod ${NC}"
			} || echo -e "${RED}  - Missing: $mod ${NC}"
		done

		# ----------- MODULE + VENDOR_DLKM IMAGE LOGIC -----------
		echo -e "${YELLOW} Copying modules and configs to vendor_dlkm ${NC}"

		DLKM_MODULE_DIR="$ANYKERNEL_PATH/vendor_dlkm/lib/modules"
		mkdir -p "$DLKM_MODULE_DIR"

		MOD_INST_PATH=$(find "$TARGET_OUT/modules_inst" -name "modules.dep" -print -quit | xargs dirname)

		for cfg in "modules.alias" "modules.dep" "modules.softdep"; do
			cfg_path=$(find "$TARGET_OUT/modules_inst" -type f -name "$cfg" -print -quit)
			[ -f "$cfg_path" ] && {
				cp -f "$cfg_path" "$DLKM_MODULE_DIR/"
				echo -e "${GREEN}  + Added config to vendor_dlkm: $cfg ${NC}"
			} || echo -e "${RED}  - Missing config: $cfg ${NC}"
		done

		[ -f "$MOD_INST_PATH/modules.order" ] && {
			sed 's/.*\///g' "$MOD_INST_PATH/modules.order" | sed 's/^wlan.ko$/qca_cld3_wlan.ko/g' > "$DLKM_MODULE_DIR/modules.load"
			sed -i -E '/^(hlcan|slcan|can327|8188eu|88XXau|8814au)\.ko$/d' "$DLKM_MODULE_DIR/modules.load"
			echo -e "${GREEN}  + Added config to vendor_dlkm: modules.load (Original order kept) ${NC}"
		} || echo -e "${RED}  - Missing config: modules.order (cannot create modules.load) ${NC}"

		# ----------- Creating vendor_dlkm.img -----------
		echo -e "${YELLOW}-3 Creating vendor_dlkm.img ${NC}"

		TEMP_DIR="$TARGET_OUT/vendor_dlkm_files"
		MODULE_DIR="$TEMP_DIR/lib/modules"

		mkdir -p "$MODULE_DIR"

		# Copy all kernel modules
		find "$TARGET_OUT/modules_inst" -type f -name "*.ko" -exec cp {} "$MODULE_DIR/" \;

		# ── Rename wlan.ko → qca_cld3_wlan.ko ────────────────────────────────────────
		[ -f "${MODULE_DIR}/wlan.ko" ] && {
			mv "${MODULE_DIR}/wlan.ko" "${MODULE_DIR}/qca_cld3_wlan.ko" ||
			echo -e "${RED}Failed to rename wlan.ko → qca_cld3_wlan.ko${NC}"
			echo -e "${GREEN}  + Renamed wlan.ko → qca_cld3_wlan.ko${NC}"
		} || echo -e "${PURPLE}  ~ wlan.ko not found — skipping rename${NC}"

		# Copy additional vendor_dlkm files from AnyKernel3 (etc/lib) if they exist
		[ -d "$ANYKERNEL_PATH/vendor_dlkm/etc" ] && {
			mkdir -p "$TEMP_DIR/etc"
			cp -r "$ANYKERNEL_PATH/vendor_dlkm/etc/"* "$TEMP_DIR/etc/"
		}

		[ -d "$ANYKERNEL_PATH/vendor_dlkm/lib" ] && {
			mkdir -p "$TEMP_DIR/lib"
			cp -r "$ANYKERNEL_PATH/vendor_dlkm/lib/"* "$TEMP_DIR/lib/"
		}

		rm -f "$TEMP_DIR/lib/placeholder"

		# Fix paths in modules.dep for mounting in /vendor
		[ -f "$MODULE_DIR/modules.dep" ] && {
			sed -i 's/wlan.ko/qca_cld3_wlan.ko/g' "$MODULE_DIR/modules.dep"
			sed -i 's@\(^kernel/[^: ]*/\)\([^: ]*\.ko\)@/vendor/lib/modules/\2@g' "$MODULE_DIR/modules.dep"
		}

		# ----------- Creating modules.blocklist -----------
		echo -e "${YELLOW} Generating modules.blocklist ${NC}"
		echo -e "# The module will be loaded from the target RC after\n# cold boot calibration.\nblocklist qca_cld3_wlan" > "$MODULE_DIR/modules.blocklist"
		echo "blocklist hlcan" >> "$MODULE_DIR/modules.blocklist"
		echo "blocklist slcan" >> "$MODULE_DIR/modules.blocklist"
		echo "blocklist can327" >> "$MODULE_DIR/modules.blocklist"
		echo "blocklist 8188eu" >> "$MODULE_DIR/modules.blocklist"
		echo "blocklist 88XXau" >> "$MODULE_DIR/modules.blocklist"
		echo "blocklist 8814au" >> "$MODULE_DIR/modules.blocklist"
		echo -e "${GREEN}  + Added modules.blocklist ${NC}"

		# Create vendor_dlkm.img (ext4)
		dd if=/dev/zero of="$ANYKERNEL_PATH/vendor_dlkm.img" bs=1M count=512

		MKE2FS=$MKE2FS mke2fs \
			-O "extent,huge_file" \
			-T largefile \
			-L vendor_dlkm \
			-d "$TEMP_DIR" \
			"$ANYKERNEL_PATH/vendor_dlkm.img"

		e2fsck -f "$ANYKERNEL_PATH/vendor_dlkm.img"
		resize2fs -M "$ANYKERNEL_PATH/vendor_dlkm.img"

		# Cleanup
		rm -rf "$TEMP_DIR"
		rm -rf "$ANYKERNEL_PATH/vendor_dlkm"
		} || {
		# If modules are disabled - clean AnyKernel folders for a clean ZIP
		echo -e "${PURPLE} CONFIG_MODULES is not set → Skipping all module logic ${NC}"
		rm -rf "$ANYKERNEL_PATH/vendor_dlkm"
		rm -rf "$ANYKERNEL_PATH/vendor_ramdisk"
		rm -rf "$ANYKERNEL_PATH/ramdisk"
		rm -f  "$ANYKERNEL_PATH/vendor_dlkm.img"
	}

	# ---------------- Packing ----------------
	echo -e "${YELLOW} Packing flashable kernel ${NC}"

	CURRENT_TIME="${CURRENT_TIME:-$(date +"%Y%m%d-%H%M")}"
	CLEAN_TIME="$(echo "$CURRENT_TIME" | sed 's/[^a-zA-Z0-9._-]//g')"

	cd "$ANYKERNEL_PATH" || {
		echo -e "${RED}Failed to enter $ANYKERNEL_PATH${NC}"
		exit 1
	}

	FLASHABLE_ZIP="Sakura-$CLEAN_TIME.zip"
	SIGNED_ZIP="Sakura-$CLEAN_TIME-signed.zip"

	zip -q -r "$FLASHABLE_ZIP" * \
		-x README.md \
		   changelog.txt \
		   defconfig \
		   kernel-changelog.txt \
		   build.log || {
		echo -e "${RED}Failed to pack flashable kernel${NC}"
		exit 1
	}

	echo -e "${YELLOW} Signing ZIP file securely... ${NC}"
	sign_zip "$FLASHABLE_ZIP" "$SIGNED_ZIP"

	echo -e "${GREEN}✔ Flashable signed zip ready:${NC} $TARGET_OUT/$ANYKERNEL_PATH/$SIGNED_ZIP"
	curl -s -X POST http://127.0.0.1:8080/webhook/build_ready > /dev/null

	cd "$KERNEL_DIR" || return 1
}

# ---- Save kernel configuration (Always Save) ----
save_defconfig() {
	echo -e "${YELLOW}------------------------------${NC}"
	echo -e "${YELLOW} Saving kernel configuration...${NC}"
	echo -e "${YELLOW}------------------------------${NC}"

	[ -f "$TARGET_OUT/.config" ] && {
		cp "$TARGET_OUT/.config" "$AK3_PATH/defconfig"
		END_SEC=$(date +%s)
		COST_SEC=$((END_SEC - START_SEC))
		echo -e "${YELLOW}Completed. Kernel configuration saved to ${AK3_PATH}/defconfig${NC}"
		echo -e "${YELLOW}Kernel configuration save took ${COST_SEC} seconds.${NC}"
	} || echo -e "${RED}Error: '$TARGET_OUT/.config' not found. Cannot save configuration.${NC}"
}

# ----  Clean ----
clean() {
	echo -e "${YELLOW}Cleaning source tree and build files...${NC}"
	make mrproper -j$THREAD > /dev/null 2>&1
	make clean -j$THREAD > /dev/null 2>&1
	rm -rf $TARGET_OUT
	rm -rf .config
	rm -rf output
	echo -e "${GREEN}Clean completed.${NC}"
}

# ---- Setup colour for the script ----
purple='\033[0;35m'

# Function to show an informational message
msg() {
	echo -e "\e[1;32m$*\e[0m"
}

[Aerr() {
	echo -e "\e[1;41m$*\e[0m"
	exit 1
}

# ---- Function to create a changelog file with the last 200 commits and move it to $TARGET_OUT ----
create_changelog() {
	# Define the filename for the changelog
	local changelog_file="changelog.txt"

	# Use git log to get the last 400 commits and format them
	git log -n 200 --pretty=format:"%h - %s (%an)" > "$changelog_file"

	# Print the location of the changelog file
	msg "${purple}Changelog saved to $changelog_file ${white}"

	# Add a dash before each commit line for better readability
	sed -i -e "s/^/- /" "$changelog_file"

	# Move the changelog file to $TARGET_OUT
	mv "$changelog_file" "$AK3_PATH/"
}

# ----  End Build Info ----
# Function to display kernel version and config information
display_kernel_version_info() {
	# Find sakura_defconfig file
	SAKURA_DEFCONFIG=$(find . -name 'sakura_defconfig' -print -quit)

	[ -z "$SAKURA_DEFCONFIG" ] && echo -e "${RED}sakura_defconfig not found!${NC}" && return 1

	echo -e "${GREEN}===================END_BUILD=================${NC}"
	echo -e "${PURPLE}***************Sakura-Kernel**************${NC}"
	echo -e "USER: $KBUILD_USER"
	echo -e "HOST: $KBUILD_HOST"
	echo -e "${PURPLE}*************last commit details************${NC}"
	echo -e "Last commit (name): $(git log -1 --pretty=format:%s)"
	echo -e "Last commit (hash): $(git log -1 --pretty=format:%H)"
	echo -e "${PURPLE}********************************************${NC}"
	echo -e "VERSION: $(grep -E '^VERSION =' Makefile | awk '{print $3}')"
	echo -e "PATCHLEVEL: $(grep -E '^PATCHLEVEL =' Makefile | awk '{print $3}')"
	echo -e "SUBLEVEL: $(grep -E '^SUBLEVEL =' Makefile | awk '{print $3}')"
	echo -e "EXTRAVERSION: $(grep -E '^EXTRAVERSION =' Makefile | awk '{print $3}')"
	echo -e "NAME: $(grep -E '^NAME =' Makefile | awk '{print $3}')"
	echo -e "CONFIG_LOCALVERSION: $(grep -E '^CONFIG_LOCALVERSION=' $SAKURA_DEFCONFIG | awk -F'=' '{print $2}')"
	echo -e "CONFIG_UNAME_OVERRIDE_STRING: $(grep -E '^CONFIG_UNAME_OVERRIDE_STRING=' $SAKURA_DEFCONFIG | awk -F'=' '{print $2}')"
	echo -e "${PURPLE}**********************************************${NC}"
}

setup_toolchains() {
	local TOOLCHAIN_DIR="$(pwd)/../Madara-Built"
	echo -e "${YELLOW}Setting up toolchains in $TOOLCHAIN_DIR...${NC}"
	mkdir -p "$TOOLCHAIN_DIR"

	# Sakura Clang
	[ -f "$TOOLCHAIN_DIR/SAKURA/bin/clang" ] || {
		echo -e "${GREEN}Downloading Sakura Clang...${NC}"
		mkdir -p "$TOOLCHAIN_DIR/SAKURA"
		local DOWNLOAD_URL=$(curl -sL "https://api.github.com/repos/Madara273/Sakura-Clang-Compiler/releases/tags/Sakura-Clang-R01" | grep -oP '"browser_download_url": "\K[^"]+' | head -n 1)
		local FILE="sakura-clang-temp.tar.zst"

		[ -n "$DOWNLOAD_URL" ] || { echo -e "${RED}Error: Release asset not found!${NC}"; rm -rf "$TOOLCHAIN_DIR/SAKURA"; return 1; }

		wget -q --show-progress -L "$DOWNLOAD_URL" -O "$TOOLCHAIN_DIR/$FILE" || {
			echo -e "${RED}Error downloading Sakura Clang!${NC}"
			rm -rf "$TOOLCHAIN_DIR/$FILE" "$TOOLCHAIN_DIR/SAKURA"
			return 1
		}

		tar --zstd -xf "$TOOLCHAIN_DIR/$FILE" -C "$TOOLCHAIN_DIR/SAKURA" --strip-components=1
		rm -f "$TOOLCHAIN_DIR/$FILE"
	}

	# Eva GCC (Fixed)
	[ -f "$TOOLCHAIN_DIR/EVA/bin/aarch64-elf-gcc" ] || {
		echo -e "${GREEN}Downloading Eva GCC...${NC}"
		mkdir -p "$TOOLCHAIN_DIR/EVA"
		local VERSION=$(curl -sL https://github.com/mvaisakh/gcc-build/releases/latest | grep -oE "tag/[0-9]+" | head -1 | cut -d/ -f2)
		local FILE="eva-gcc-arm64-$VERSION.xz"
		wget -q --show-progress "https://github.com/mvaisakh/gcc-build/releases/download/$VERSION/$FILE" -O "$TOOLCHAIN_DIR/$FILE"
		tar -xf "$TOOLCHAIN_DIR/$FILE" -C "$TOOLCHAIN_DIR/EVA" --strip-components=1
		rm -f "$TOOLCHAIN_DIR/$FILE"
	}

	echo -e "${GREEN}Latest toolchains are ready.${NC}"
}

# ---- Kernel compilation function ----
compile_kernel() {
	[ "$COMPILER_CHOICE" == "1" ] && CURRENT_COMPILER="EVA GCC"
	[ "$COMPILER_CHOICE" == "2" ] && CURRENT_COMPILER="Sakura Clang"
	[ "$COMPILER_CHOICE" != "1" ] && [ "$COMPILER_CHOICE" != "2" ] && CURRENT_COMPILER="Sakura Clang"

	CURRENT_BRANCH=$(git branch --show-current 2>/dev/null || echo "unknown")

	curl -s -X POST "http://127.0.0.1:8080/webhook/start?compiler=$(echo "$CURRENT_COMPILER" |
	sed 's/ /%20/g')&branch=$(echo "$CURRENT_BRANCH" | sed 's/ /%20/g')" > /dev/null

	trap 'echo -e "${RED}Build interrupted! Sending failed status...${NC}"; curl -s -X POST http://127.0.0.1:8080/webhook/failed > /dev/null; exit 1' INT TERM

	random_color
	ascii_art_logo
	setup_toolchains
	clean
	check_tools
	clone_anykernel3
	make_defconfig
	display_build_info
	create_changelog
	save_defconfig

	{
		build_kernel && \
		link_all_dtb_files && \
		generate_flashable && \
		display_kernel_version_info
		} || {
		echo -e "${RED}Error: Kernel build failed. Sending status...${NC}"
		curl -s -X POST http://127.0.0.1:8080/webhook/failed > /dev/null
		exit 1
	}
}

choose_action

echo -e "${GREEN}Done.${NC}"

# END CUSTOM BUILD KERNEL SCRIPTS
