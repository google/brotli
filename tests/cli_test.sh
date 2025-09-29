#!/bin/bash

source gbash.sh || exit
source module gbash_unit.sh

readonly MKTEMP='/bin/mktemp'
readonly RM='/bin/rm'

function test::brotli_cli::setup() {
  TEMP_DIR=$(${MKTEMP} -d)
  LOG INFO ${TEMP_DIR}
  BROTLI_PKG="${RUNFILES}/google3/third_party/brotli"
  BROTLI="${BROTLI_PKG}/tools/brotli"
  cd ${TEMP_DIR}
  echo "Kot lomom kolol slona" > text.orig
  echo "Lorem ipsum dolor sit amet. " > ipsum.orig
}

function test::brotli_cli::teardown() {
  unset BROTLI
  unset BROTLI_PKG
  ${RM} -rf ${TEMP_DIR}
  unset TEMP_DIR
}

function test::brotli_cli::roundtrip() {
  ${BROTLI} -Zfk text.orig -o text.br
  ${BROTLI} -d text.br -o text.unbr
  EXPECT_FILE_CONTENT_EQ text.orig text.unbr
}

# 'SGVsbG8=' == $(echo -n "Hello" | base64)

function test::brotli_cli::comment_ok() {
  ${BROTLI} -Zfk -C SGVsbG8= text.orig -o text.br
  ${BROTLI} -d --comment=SGVsbG8= text.br -o text.unbr
  EXPECT_FILE_CONTENT_EQ text.orig text.unbr
}

function test::brotli_cli::comment_no_padding() {
  ${BROTLI} -Zfk -C SGVsbG8 text.orig -o text.br
  ${BROTLI} -d --comment=SGVsbG8= text.br -o text.unbr
  EXPECT_FILE_CONTENT_EQ text.orig text.unbr
}

function test::brotli_cli::comment_extra_padding() {
  ${BROTLI} -Zfk -C SGVsbG8 text.orig -o text.br
  ${BROTLI} -d --comment=SGVsbG8== text.br -o text.unbr
  EXPECT_FILE_CONTENT_EQ text.orig text.unbr
}

function test::brotli_cli::comment_ignored() {
  ${BROTLI} -Zfk -C SGVsbG8= text.orig -o text.br
  EXPECT_SUCCEED "${BROTLI} -d text.br -o text.unbr"
}

function test::brotli_cli::comment_mismatch_content() {
  ${BROTLI} -Zfk --comment=SGVsbG8= text.orig -o text.br
  EXPECT_FAIL "${BROTLI} -dC SGVsbG7= text.br -o text.unbr"
  EXPECT_FAIL "${BROTLI} -tC SGVsbG7= text.br"
}

function test::brotli_cli::comment_mismatch_length() {
  ${BROTLI} -Zfk --comment=SGVsbG8= text.orig -o text.br
  EXPECT_FAIL "${BROTLI} -tC SGVsbA== text.br"
}

function test::brotli_cli::comment_too_much_padding() {
  EXPECT_FAIL "${BROTLI} -Zfk -C SGVsbG8=== text.orig -o text.br"
}

function test::brotli_cli::comment_padding_in_the_middle() {
  EXPECT_FAIL "${BROTLI} -Zfk -C SGVsbG=8 text.orig -o text.br"
}

function test::brotli_cli::comment_ignore_tab_cr_lf_sp() {
  COMMENT=$'S\tG\rV\ns bG8='
  EXPECT_SUCCEED "${BROTLI} -Zfk -C \"${COMMENT}\" text.orig -o text.br"
}

function test::brotli_cli::comment_invalid_chars() {
  EXPECT_FAIL "${BROTLI} -Zfk -C S.GVsbG8= text.orig -o text.br"
}

function test::brotli_cli::concatenated() {
  ${BROTLI} -Zfk ipsum.orig -o one.br
  ${BROTLI} -Zfk text.orig -o two.br
  cat one.br two.br > full.br
  EXPECT_FAIL "${BROTLI} -dc full.br  > full.unbr"
  EXPECT_SUCCEED "${BROTLI} -dKc full.br > full.unbr"
  EXPECT_SUCCEED "${BROTLI} -dc --concatenated full.br > full.unbr"
  cat ipsum.orig text.orig > full.orig
  EXPECT_FILE_CONTENT_EQ full.orig full.unbr
}

gbash::unit::main "$@"
