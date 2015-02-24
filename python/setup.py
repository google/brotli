from distutils.core import setup, Extension
from distutils.command.build_ext import build_ext
import platform
from os.path import abspath

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
                        abspath("../enc/backward_references.cc"),
                        abspath("../enc/block_splitter.cc"),
                        abspath("../enc/brotli_bit_stream.cc"),
                        abspath("../enc/encode.cc"),
                        abspath("../enc/entropy_encode.cc"),
                        abspath("../enc/histogram.cc"),
                        abspath("../enc/literal_cost.cc"),
                        abspath("../dec/bit_reader.c"),
                        abspath("../dec/decode.c"),
                        abspath("../dec/huffman.c"),
                        abspath("../dec/safe_malloc.c"),
                        abspath("../dec/streams.c"),
                    ],
                    depends=[
                        abspath("../enc/backward_references.h"),
                        abspath("../enc/bit_cost.h"),
                        abspath("../enc/block_splitter.h"),
                        abspath("../enc/brotli_bit_stream.h"),
                        abspath("../enc/cluster.h"),
                        abspath("../enc/command.h"),
                        abspath("../enc/context.h"),
                        abspath("../enc/dictionary.h"),
                        abspath("../enc/encode.h"),
                        abspath("../enc/entropy_encode.h"),
                        abspath("../enc/fast_log.h"),
                        abspath("../enc/find_match_length.h"),
                        abspath("../enc/hash.h"),
                        abspath("../enc/histogram.h"),
                        abspath("../enc/literal_cost.h"),
                        abspath("../enc/port.h"),
                        abspath("../enc/prefix.h"),
                        abspath("../enc/ringbuffer.h"),
                        abspath("../enc/static_dict.h"),
                        abspath("../enc/transform.h"),
                        abspath("../enc/write_bits.h"),
                        abspath("../dec/bit_reader.h"),
                        abspath("../dec/context.h"),
                        abspath("../dec/decode.h"),
                        abspath("../dec/dictionary.h"),
                        abspath("../dec/huffman.h"),
                        abspath("../dec/prefix.h"),
                        abspath("../dec/safe_malloc.h"),
                        abspath("../dec/streams.h"),
                        abspath("../dec/transform.h"),
                        abspath("../dec/types.h"),
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
