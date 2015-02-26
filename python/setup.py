from distutils.core import setup, Extension
from distutils.command.build_ext import build_ext
import platform

class BuildExt(build_ext):
    def get_source_files(self):
        filenames = build_ext.get_source_files(self)
        for ext in self.extensions:
            filenames.extend(ext.depends)
        return filenames

    def build_extension(self, ext):
        c_sources = []
        cxx_sources = []
        for source in ext.sources:
            if source.endswith(".c"):
                c_sources.append(source)
            else:
                cxx_sources.append(source)
        extra_args = ext.extra_compile_args or []

        objects = []
        for lang, sources in (("c", c_sources), ("c++", cxx_sources)):
            if lang == "c++": 
                if self.compiler.compiler_type in ["unix", "cygwin", "mingw32"]:
                    extra_args.append("-std=c++0x")

            macros = ext.define_macros[:]
            if platform.system() == "Darwin":
                macros.append(("OS_MACOSX", "1"))
            for undef in ext.undef_macros:
                macros.append((undef,))

            objs = self.compiler.compile(sources,
                                         output_dir=self.build_temp,
                                         macros=macros,
                                         include_dirs=ext.include_dirs,
                                         debug=self.debug,
                                         extra_postargs=extra_args,
                                         depends=ext.depends)
            objects.extend(objs)

        self._built_objects = objects[:]
        if ext.extra_objects:
            objects.extend(ext.extra_objects)
        extra_args = ext.extra_link_args or []

        ext_path = self.get_ext_fullpath(ext.name)
        # Detect target language, if not provided
        language = ext.language or self.compiler.detect_language(sources)

        self.compiler.link_shared_object(
            objects, ext_path,
            libraries=self.get_libraries(ext),
            library_dirs=ext.library_dirs,
            runtime_library_dirs=ext.runtime_library_dirs,
            extra_postargs=extra_args,
            export_symbols=self.get_export_symbols(ext),
            debug=self.debug,
            build_temp=self.build_temp,
            target_lang=language)

brotli = Extension("brotli",
                    sources=[
                        "brotlimodule.cc",
                        "enc/backward_references.cc",
                        "enc/block_splitter.cc",
                        "enc/brotli_bit_stream.cc",
                        "enc/encode.cc",
                        "enc/entropy_encode.cc",
                        "enc/histogram.cc",
                        "enc/literal_cost.cc",
                        "dec/bit_reader.c",
                        "dec/decode.c",
                        "dec/huffman.c",
                        "dec/safe_malloc.c",
                        "dec/streams.c",
                    ],
                    depends=[
                        "enc/backward_references.h",
                        "enc/bit_cost.h",
                        "enc/block_splitter.h",
                        "enc/brotli_bit_stream.h",
                        "enc/cluster.h",
                        "enc/command.h",
                        "enc/context.h",
                        "enc/dictionary.h",
                        "enc/encode.h",
                        "enc/entropy_encode.h",
                        "enc/fast_log.h",
                        "enc/find_match_length.h",
                        "enc/hash.h",
                        "enc/histogram.h",
                        "enc/literal_cost.h",
                        "enc/port.h",
                        "enc/prefix.h",
                        "enc/ringbuffer.h",
                        "enc/static_dict.h",
                        "enc/transform.h",
                        "enc/write_bits.h",
                        "dec/bit_reader.h",
                        "dec/context.h",
                        "dec/decode.h",
                        "dec/dictionary.h",
                        "dec/huffman.h",
                        "dec/prefix.h",
                        "dec/safe_malloc.h",
                        "dec/streams.h",
                        "dec/transform.h",
                        "dec/types.h",
                    ],
                    language="c++",
                    )

setup(
    name="Brotli",
    version="0.1",
    url="https://github.com/google/brotli",
    description="Python binding of the Brotli compression library",
    author="Khaled Hosny",
    author_email="khaledhosny@eglug.org",
    license="Apache 2.0",
    ext_modules=[brotli],
    cmdclass={'build_ext': BuildExt},
)
