# Fast and efficient recompression using previous compression artifacts


# **Introduction**

High quality brotli compression provides great compression rates, but at significant CPU and time costs. As a result it's often not feasible to perform high quality brotli compression for dynamic or even partially dynamic content, and servers compromise the compression rates in order to achieve on-the-fly compression speeds for those payloads.

This document describes a way to improve the brotli encoder such that on-the-fly compression speeds **and** high compression rates are both possible for compression payloads with high similarity to previously compressed content.

There are many cases where we'd want to compress dynamic resources that are very similar to static ones we compressed before. In those cases it can be useful to use some information from the past compression. 

Brotli has different types of artifacts that it calculates during compression which are stored in the compressed file, such as block splittings, block clusterization, backward references, entropy coding decisions, etc.

High quality brotli compression (e.g. brotli level 11) invests a lot of CPU time in finding those ideal artifacts for the file at hand.

Would it help if we’ve already compressed a similar file before and calculated all the artifacts needed?

Let’s say we have a compressed file A and want to compress file B which is quite similar to A. For example, we’ve removed or changed some part of file A and saved it to file B. 

Statistics that Brotli calculates during compression would be quite similar for both files. That means that we can utilize some of the artifacts from file A’s compression when compressing B, which can provide gains in both compression speed and rates.

In this document we’ll explore reusing are backward references and block splits as artifacts.


# **Executive Summary**

The approach explored here can benefit cases when we’re trying to compress dynamic content on-the-fly, where the content resembles content available to us ahead of time. Real life examples include delivery of JS bundles subsets (e.g. when parts of the code are already cached on the client), dynamic HTML based on known-in-advance templates, and subsetted WOFF2 fonts.

With this approach we’ve seen 5.3% better compression rates **and** 39% faster compression speeds when removing 10% of the content, and 2.9% better compression rates and 32% faster compression, when removing 50% of the content.

Read on to see how this was achieved, or skip to the full [results](#results) below.


# **Motivation**

Currently, high-quality brotli compression of dynamic content is simply not feasible - compressing the content simply takes too long (in the order of seconds), and the benefits of compression cannot compensate for that.

At the same time, there are many scenarios where dynamic content is simply a variation of previously-known static content:



*   When serving concatenated JS bundles, the exact composition of the bundle is not always known ahead of time (e.g. due to client-side caching of some chunks). But, all potential bundles are a subset of one a superset concatenation of all possible chunks, which is known at build time.
*   When serving dynamic HTML content, some of the content may be dynamic (generated on-the-fly, using data typically coming from a database), but large parts of the content are static and dictated by the template. That results in high-similarities between the dynamic content and the originating, static template.
*   When dynamically subsetting WOFF2 based fonts, one could reuse the full font’s compression information in order to achieve high compression speeds without compromising on compression rates.


## **Specific purpose for web bundles serving**

Web Bundles can provide us a [delivery mechanism](https://docs.google.com/document/d/11t4Ix2bvF1_ZCV9HKfafGfWu82zbOD7aUhZ_FyDAgmA/edit#) for large applications built of many separate smaller chunks. Instead of getting each script as a separate file we would get a single blob containing all JS scripts of an app using Web Bundle.

Web Bundle allows us to compress the delivered resources as a single compressed resource. So in case of Web Bundles we would get much better and faster compression of an app then for unbundled approach as it’s better and faster to compress one large file instead of a lot of small ones.

The results of comparing compression for bundled and unbundled approaches can be found [here](https://docs.google.com/spreadsheets/d/1RA-9QEUSFR5JIgKeVBZaknjee-YI9N6PYgIWwhM3Hxo/edit?usp=sharing). 

However, some chunks can be used in multiple different bundles and to avoid delivery of chunks that we already have in cache we need to subtract them from the bundle. 

For each chunk in the bundle, the server will check if it exists in the hashlist of already delivered chunks, and remove it from the bundle if it doesn’t.

Our proposal is a way to remove some chunks from a compressed bundle in a fast way while maintaining high compression quality.

Let’s assume that we have a Brotli-compressed Web Bundle, and we know that we already have some chunks of that bundle in a cache, so we want to remove those chunks from the bundle.

A simple approach would be to decompress a file, delete some chunks and compress it again. Unfortunately that approach is either slow or results in sub-par compression, as high-quality compression takes a lot of time. 

Let’s reformulate a task a bit:

Let file A be a Web Bundle and file B - a bundle that we would get after removing chunks that we already fetched from file A. We’ve already compressed file A and want to compress file B.

Now you can see that this is exactly the same task as described in the previous section.

By reusing the information from file A’s compression process, we can compress file B much faster or/and better. 


# **Background**

In this section we’ll describe some basics of Brotli that are relevant to the task. Note that this is not a comprehensive guide to everything Brotli related. There are many other techniques and algorithms used inside Brotli which are out-of-scope for this document.

Brotli calculates different types of artifacts and stores them in the compressed file. Below are the types which we will work with:



-  **Backward references**

Backward references enable the compression engine to describe a portion of text that was already previously encountered, and that can replace the current part of the text.


Backward reference is described as &lt;copy_length, distance>. There are 2 types of references: a usual backward reference (which is referring to a previously encountered string in the resource itself) and a reference to Brotli’s static dictionary.


_Usual backward reference:_


Backward reference found at position _pos_ of initial text means that


_text[pos:pos + copy_length] = text[pos - distance:pos - distance + copy_length]._


So there is no need to save string _text[pos:pos + copy_length]_ in a compressed file but just store backward reference &lt;copy_length, distance> on that position. This reference will point to the part of the text with the same substring.


_Reference to the static dictionary:_


The idea behind those references is the same as before: standing at position _pos_ find an equal substring. The only difference is that instead of pointing to some substring encountered before in the text, this reference will point to the static dictionary.


A static dictionary is a fixed-in-advance text of the most popular words and is the same for all the files. The structure of such a reference is the same &lt;copy_length, distance> where distance describes the offset in the dictionary (need to apply some transformations to distance to get it). To distinguish between these two types of references, dictionary references have distance that is greater then the maximum possible backward distance for the current position. 





A reference is saved as part of an &lt;`insert_length, copy_length, distance`> command which is stored in the compressed file.  


`insert_length` defines the number of literals that should be inserted before copying the string, `copy_length` defines the size of the string to be copied and `distance` defines the position of the string to be copied, in characters before the position of the copied string.


If we have found such a command in a certain position in the file, that means that we need to take `insert_length` symbols as a result and find the backward `distance` after them, go back `distance` symbols to find its contents, and then copy `copy_length` symbols from the latter position to the former position.


_Example_:


For the text **2foobarchromefoobarapplearch** one can find 2 pairs of equal substring:
**foobar** and **arch**.


The corresponding references are 



*   &lt;6, 12> for _foobar_ substring (copy_length=6, distance=12)
*   &lt;4, 19> for _arch_ substring (copy_length=4, distance=19)

So we can replace the text by  **2foobarchrome&lt;6, 12>apple&lt;4, 19>**.


To get commands from it we need to calculate the insert length which is simply the amount of symbols from the previous backward reference.


For the first reference there are 13 letters before the reference, so the command would be &lt;13, 6, 12>. For the second one, there are 5 literals from the previous reference, thus it’s &lt;5, 4, 19>.


Now we have all the commands for the file and the compressed file could be modified using those commands to:

**&lt;13, 6, 2foobarchrome, 12>&lt;5, 4, apple, 19>**

or more accurately to

**F(13, 6)2foobarchrome12F(5, 4)apple19**

where `F(a, b)` is a number that we can derive `a` and `b` from.


During compression Brotli finds those equal substrings and stores backward references in the compressed file. During decompression, the engine takes those references and reverts them back to the substring they represent.


The different levels of Brotli have different reference search algorithms, which become more complex and slow as quality level increases. 


When reusing past compression artifacts, one goal is to decrease the time taken for reference search in high Brotli levels while the quality of backward references stays the same.


Another goal is to make backward references for smaller quality levels better while the computation time doesn’t change. 



*   **Block splitting**

Brotli splits initial text to blocks. These blocks don’t impact backward reference search, but they do impact the [Huffman encoding](https://en.wikipedia.org/wiki/Huffman_coding) which is later applied to the data. 


For each block, Brotli picks a different set of Huffman trees that it applies to symbols inside the block, in order to further compress it. The better block splitting is, the better compression Huffman encoding achieves.


There are 3 types of block splits:

*   Block splits for literals
*   Block splits for insert and copy lengths
*   Block splits for distances

These 3 types are independent of each other.


Block splitting becomes more complex as the quality level grows. Low-quality levels split text to the blocks of fixed predefined size sometimes merging close blocks if needed while higher-quality levels try to find homogeneous parts of the text and define them as blocks, as they have similar character distribution statistics, leading to more efficient encoding. 


When reusing past compression artifacts, one goal is to find block splits faster for high quality levels while maintaining the rate of compression.


Another is to improve lower level block splits, without a decrease in speed.


# **Design**

Let’s say we have 2 similar files: A, B. 

Here we will focus on the case where B was obtained by deleting some text part from file A but this approach could be quite easily generalized for other relationships between A and B.

Assume that we’ve already compressed file A. Would the information from this compression help to compress file B? It would!

_Example:_

If we have a set of backward references for file A most of these backward references could be reused in file B compression.

Let’s consider text B which is B = A[:a] + A[b:<zero-width space>] (removed [a, b) positions from text A).

A set of backward references for A text is {&lt;_pos, copy_len, distance_>, ....}.



*   If _pos_ and _pos + copy_len_ lie before the removing content then we can reuse the corresponding backward reference for text B compression.
*   If _pos_ and_ pos - distance_ lie after removing substring then we can reuse these backward references to compress text B.

There are more cases to consider but the general idea is that in such a case we can use information about backward references from the first compression.

Similarly for block splitting:

If we have block split positions from text A compression {_pos_1, pos_2, pos_3,_ ….} where a pair (_pos_i, pos\_{i + 1}_) corresponds to one block then we can use this information to compress file B. 



*   If _pos_i_ and _pos\_{i + 1}_ lie before the deleted content that was removed then we can use the same block for file B compression.
*   If _pos_i_ and _pos\_{i + 1}_  lie after removing part then we can also reuse this block split. The only thing we need to do before reusing is to decrease both _pos_i_ and _pos\_{i + 1}_ by (b - a) as positions after the part that has been deleted should shift.


## **General approach**

We used the following steps to prototype the above approach:



1. Get high-quality brotli compressed file A as input
2. Decompress A and save all the information needed from its past compression.
3. Delete certain chunks from file A to get file B. 
4. Change the information collected so it’s suitable for B.
5. Using this information compress file B.

It’s worth noting that other than step (5), the above steps are all rather fast to execute.

Step (5) is more computationally intensive but should be fast enough as we use the information from A’s compression in order to avoid re-calculating it for B..

Note that step (3) above is suitable for our proof-of-concept project, but may not be suitable as a production-level API, where the decision which ranges to remove from A needs to be given as input.


## **Saving information in the decoding phase**


### **Backward references collection**

While decompressing a file we will collect the backward references found.

The backward reference structure that we’ll store would be as follows:


&lt;_position, copy_len, distance, max_distance_>

where 



*   _position_ corresponds to the position this backward reference was found at during encoding.
*   _copy_len_ means an amount of literals that should be copied whether from the preceding text or from the static dictionary.
*   _distance_ describes an backwards offset where the substring exists (in either preceding text or static dictionary, depending on the reference type).
*   _max_distance_ is the maximum distance possible for the current position for a usual backward reference. If the _distance_ is bigger then this value the reference points to the static dictionary.

The decoder maintains state where it stores decompression information. We will additionally store a vector of backward references as part of that state.


### **Block splits collection**

Alongside backward references, we will save the block splits for literals as well as insert and copy lengths. We’ll store and work with those different types of blocks separately (since Brotli also works with them separately). 

Both Brotli encoder and decoder work with metablocks which are divided into blocks. Thus, they find and operate with block splits inside different metablocks separately. 

After a file change (deleted a part of it) the metablocks for the new file could be different from the ones for the original file. If during decompression we save block splits separately for different metablocks (the way Brotli works with them), we’ll need to jump between metablocks in the encoder stage which makes everything harder (although possible). Therefore we will save all the block splits for the original file together in one array and won’t separate them by metablocks.

One problem with that approach is related to block types. There are several rules for block types inside one metablock:



*   The first block in metablock has to have a block type 0.
*   Block type for block should be less, equal or greater by 1 then all the previous blocks inside the current metablock.

That means if we store blocks for all the metablocks together, block types, as we go through them from the beginning, can sometimes be reset to zero and start increasing again. That’s a problem if, for example, the encoder encounters a block of type 0 during encoding, as it won’t be able to tell  whether it’s a new metablock or the same block type as we’ve seen before.

_Example of the problem:_

Let’s imagine we have 2 metablocks in the original text. The first metablock has 2 blocks inside with block types 0 and 1, the second one has 4 blocks with types 0, 1, 2, 0.

If we store all the blocks together the resulting array of block types would be as follows: 0, 1, 0, 1, 2, 0.

First and third blocks in this array have the same type meaning that we should use the same Huffman tree for them. However, in the original text we used different Huffman trees for these blocks as they lie in different metablocks. As a result, the encoder would think that symbols inside blocks 1 and 3 are similar but it’s not necessarily true.

To solve such a problem we will modify block types we save in the way that block types inside the metablock should be greater than all block types in previous metablocks and have the same relationship inside the current metablock. To do so we will simply increase each block type by the number of different block types in the previous metablocks. As block type numbers don’t have any semantic meaning and we only compare them for equality this change will not affect a comparison.

It’s worth noting that this is only applied to the block types we store and don’t impact the decompression or the output.

The final structure of saving all the block splits is 
&lt;_types, positions_begin, positions_end, num_types_>

where
*   _types_ - an array of block types for all the blocks in the text.
*   _positions_begin_ - an array of positions where each block begins.
*   _positions_end_ - an array of positions where each block ends.
*   _num_types_ - overall amount of block types in the text.

During decoding we store one such a structure for literals block splits and another for insert and copy lengths block splits.


## **Change collected information to suit the compression target**

In this section, we will only consider the case where B (our compression target) was obtained by deleting byte ranges from the A (the previously compressed file)  as that is a major case for Web Bundle serving. It’s quite simple to generalize it for other cases.


### **Adjusting backward references to compression target**

Backward references from an array of backward references for A don’t exactly suit B. We need to adjust them.

Here is a simple pseudo-code on how to do so:

Let’s say we want to delete [a, b) positions from A to get B.

We will look at all the relationships between current substring (from _position_ to _position + copy_len_), substring to copy (from _position - distance_ to _position - distance + copy_len_) and [a, b) interval and decide how to change backward reference.

For each <position, copy_len, distance, max_distance> in backward_references:

If this is a reference to static dictionary (distance > max_distance):
If position < a:
Do nothing
Else:
Change position to position - (b - a) 

If this is a usual backward reference (distance <= max_distance):
If position < a:
// Check where the substring ends 
If position + copy_len < a:
Do nothing
If position + copy_len >= a:
// Need to cut copy_len
If a - position >= 4 (we want to have copy_len at least 4):
Change copy_len to a - position
If a - position < 4:
Delete this reference

If position in [a, b):
Delete this reference

If position >= b:
// Check where corresponding substring to copy lies
If position - distance in [a, b):
//Check where substring to copy ends
If position - distance + copy_len - 1 >= b + 4 (want to have copy_len >= 4):
Change position to b - distance
Change_copy_len to copy_len - (b - position + distance)
If position - distance + copy_len - 1 < b + 4:
Delete this reference
If position - distance < a:
// Check where substring to copy ends
If position - distance + copy_len - 1 < a:
Change distance to distance - (b - a)
If position - distance + copy_len - 1 >= a:
Change distance to _distance - (b - a)
Change copy_len to a - (position - distance)
If copy_len < 4:
Delete this reference 
If position - distance >= b:
Do nothing



### **Adjust block splits to compression target**

For block splits we should do something quite similar to what was done for backward references: check the relationship between (_position_begin_, _position_end_) and [_a, b_)

For each <type, position_begin, position_end> in block splits:

If position_begin < a:
If position_end <= a:
Do nothing
If position_end in (a, b]:
Change position_end to a
If position_end > b:
// Deleted content is fully inside a block, will take the part of it before and after [_a, b_)
Change position_end to position_end - (b - a)

If position_begin in [a, b):
If position_end > b:
Change position_begin to a
Change position_end to position_end - (b - a)
If position_begin >= b:
Change position_begin to position_begin - (b - a)
Change position_end to position_end - (b - a)


## **Reuse artifacts in encoder**


### **Reusing backward references array during encoding**

Brotli has a function called FindLongestMatch which takes a text and current position as the arguments and tries to find the best backward reference for this position.

That function is only used by levels 9 and below.

Besides, there are 6 different implementations of FindLongestMatch function depending on a chosen hash function to use which depends on the quality level and the initial text.

Different implementations are inside these 6 files:



*   hash_forgetful_chain_inc.h
*   hash_longest_match_inc.h
*   hash_longest_match64_inc.h
*   hash_longest_match_quickly_inc.h
*   hash_rolling_inc.h
*   hash_composite_inc.h

For the note:

The first 3 implementations check for backward references with the last 4 distances used first (as it’s more optimal to use the previous 4 distances due to encoding structure) and then check some other backward distances (what backward distances to check and how to do so is a bit different for different implementations).

FindLongestMatch in hash_longest_match_quickly_inc.h at first tries the last distance used and then examines one another distance that could fit. A hash_rolling_inc.h implementation only checks some small set of the backward distances and a  hash_composite_inc.h implementation is used in case we have two hash functions,  it calls two other implementations and takes the best result.

The designed proposal was implemented only for the first 3 implementations as 



*   hash_longest_match_quickly_inc.h and hash_rolling_inc.h are used for low quality levels (4 and less) only.
*   hash_composite_inc.h just calls other implementations where we’ve already implemented the approach.

All the implementations can be divided into two main parts: 



*   Find a good backward reference for the current position.
*   Find reference to the static dictionary for this position if a backward reference found isn’t good enough.

For the first part in case of reusing stored in decoder backward references we will:

If we have a backward reference stored for the current position in this array:
Take it and return 
Else:
Find backward reference in the usual way (the way brotli typically finds those for current implementation)
If backward reference found intersects with a reference from the backward references array:
Cut the copy_len of found reference to prevent intersection

For references to the static dictionary in case we have backward references array:

If have a stored reference to the static dict (distance >= max_distance) for this position:
Having copy_len and distance find a dictionary word and transformation needed to use.
Take a stored reference and return



For both algorithms above,  if we don’t have a backward references array from the decoder (hense want to use a usual algorithm) we’ll do the same work as before our implementation.

The main work for backward reference search is done in CreateBackwardReferences function. It returns the resulting array of backward references for the whole text. 

For some positions, it calls FindLongestMatch and if a good match is found then tries to find a better match ahead for the next couple positions. After that if a new match is better than the one previously found,it takes the new one and moves on to the beginning of that algorithm for a new position.

To handle the case of using stored backward references, the following changes are suggested:



*   If FindLongestMatch returns a match from a backward references array from decoder, then don’t try to find a better match ahead.
*   If FindLongestMatch returns a match which isn’t from a backward references array, then try to find a better match (as in the original algorithm). 
*   Then if during the search for a better match, a backward reference from the backward reference array is found, then stop searching and use that reference.


### **Reusing block splittings during encoding**

In this section we’ll go through proposals for 2 block splitting algorithms: usual block splitting and high quality block splitting. Although Brotli 9 and below don’t use high quality block splitting it could be possible to use it for our approach as the suggested approach made Brotli 9 way faster (more on that later) and we can sacrifice some speed for better quality (and better compression rates) using high quality block splitting.

**Usual block splitting**

The main work of searching for a block splits is done in BrotliBuildMetaBlockGreedyInternal function which returns a block splits for literals, commands and distances for the current metablock.

It constructs blocks of a fixed length, considers each symbol at a time and adds it to the current block. When the current block length reaches a predefined number then there are 3 options:



*   Mark current block as a new block (create a new block type for it)
*   Merge block with a previous one
*   Merge the block with second last block

To understand which option to choose, the function calculates a decrease of entropy for merged blocks and then picks the best variant.

Merging includes



*   Mark merged blocks as having the same block type
*   Combine histograms of merged blocks together

For reusing block splits collected in the decoder:



1. Find a mapping from block structure &lt;_block_type_, _position_begin_, _position_end_> collected in decoder to a structure &lt;_block_type_, _length_> used by encoder.

Having the sequence of literals and commands (insert and copy lengths) we can find the position of a literal or command in the initial text and understand which stored block it belongs to.

To find a block length we only need to find the amount of symbols with positions from _position_begin_ to _position_end_, block type will be the same as saved in the decoder for this block.

2. Instead of calculating entropy and doing one of the 3 variants of merging, we’ll merge the current block according to its block type that we know from decoder.

Thus as we already have the same block type for the blocks we want to merge we only need to combine their histograms together. 


There's also a variant of the block splitting algorithm which includes the block's context. The algorithm is very similar for both variants, but when context is involved, the algorithm also calculates histograms of the symbols' context, on top of the histograms for the symbols themselves

**High quality block splitting**

The core work for high quality block splitting is done inside the BrotliBuildMetaBlock function.

That function calls another which constructs good block splits for literals, commands and distances and returns them as an array of &lt;_block_type_, _length_> elements.

Next, having the block types for each block, it merges block histograms accordingly and then performs clustering of context so the context won’t take a lot of space.

To use block splits from the decoder, the only thing that needs changing is how we find an array of &lt;_block_type_, _length_> elements. We can use a similar method here to the one we used for usual block splitting - find a mapping from &lt;_block_type_, _position_begin_, _position_end_> array to an array of &lt;_block_type_, _length_>. The rest of the algorithm is unchanged: combine histograms and cluster the context for both literals, commands and distances.

**Making high quality block splitting faster**

As you will see later the combination of reusing backward references and block splits with a high quality block splitting algorithm gives a huge rise in compression rates for all the Brotli levels from 5 to 9. However, high quality block splitting makes things about 4 times slower (especially, clustering takes a lot of time).

As we want an on-the-fly compression,  we are ready to sacrifice compression rates a bit, to get significantly better speeds.

After examining the algorithms involved and experimenting with their parameters, here are proposed changes that may make it possible:



*   Change the maximum number of block types for distances to 1.

This means that a whole sequence of distances will belong to the one block with block type 0.

*   Make parameters optimization in clustering.

Will change 3 parameters that determine how many iterations will be performed. 

These are 

*   _max_input_histograms_ 

change from 64 to 16

*   _pairs_capacity_ 

_max_input_histograms^2_ / 2  --> _max_input_histograms_ / 4

*   _max_num_pairs_

min(64 * _num_clusters_, _num_clusters_^2 / 2) --> _num_clusters_

The values for these parameters was found by experiments

Those changes will greatly increase the speed with an acceptable drop in compression quality ([link](https://docs.google.com/spreadsheets/d/1iGqFAcTM5E6FlpeRGhP8d3eDvasGKnAXwoG_Ayzdtrs/edit?usp=sharing)).


# **Results**

This section contains the results for the main experiments. The results are listed only for Brotli 9 and Brotli 5 for simplicity. If you are interested in other experiments here is a [link](https://docs.google.com/spreadsheets/d/1iGqFAcTM5E6FlpeRGhP8d3eDvasGKnAXwoG_Ayzdtrs/edit?usp=sharing).

Experiment details:

We are assuming that we have a Brotli-11 compressed bundle and want to get a compression of the original bundle with a deleted [a, b) fragment of different sizes.

We’ve compared 2 different approaches:



*   Usual approach:

Decompress Brotli-11 compressed bundle,

Delete [a, b) part from it,

Compress it again with some level of Brotli.



*   Our approach:

Decompress Brotli-11 compressed bundle and save info needed, 

Delete [a, b) part from it and adjust the information collected,

Compress it with some level of Brotli using this information.

Here are the results for various removal rates (10%, 30%, 50%, 70%, 90%).



*   Rate - compression rate
*   Speed - compression speed in Mb/s (input_size / compression_time)

![](https://snipboard.io/hitfxm.jpg)

_Abbreviations:_


*   br = backward references
*   bs for literals = block splits for literals
*   bs for literals&cmds = block splits for literals and insert and copy length
*   usual bs = usual block splitting algorithm
*   hq bs = high quality block splitting algorithm
*   hq bs optimized = high quality block splitting algorithm with optimizations described above
*   hq bs non-optimized = high quality block splitting algorithm without any optimization

_Conclusions_:



*   For both Brotli 5 and Brotli 9 usage of backward references and block splits from first compression greatly increase the quality.
*   Backward reference reuse at level 5  compared to the usual Brotli 5 enabled us to achieve:
*   For 10% removal: 

compression rates of 3.9 , a 4.8% improvement, 1.45 times faster.

*   For 50% removal: 

compression rates of 3.52, a 2.9% improvement, 1.32 times faster.

*   Additionally adding the reuse of block splits with usual block splitting to Brotli 5 and comparing them to the results of reusing backward references only:
*   For 10% removal: 

compression rates of 3.92, a 0.52% improvement, 1.1  times slower.

*   For 50% removal: 

compression rates of 3.53, a 0.38% improvement,  1.1 times slower.

*   Adding the use of block splits to backward references with a high quality block splitting algorithm at level 5 compared to Brotli 5 with reuse of backward references only :
*   For 10% removal: 

compression rates of 3.975, a 1.8% improvement, 3.2 times slower.

*   For 50% removal: 

compression rates of 3.577 , a 1.6% improvement, 3.4 times slower.

*   As Brotli 5 and 9 use usual block splitting by default, reuse of information also speeds up the compression (Compare _usual_ and _reuse_ approach _with usual block splitting_).
*   Adding the hiqh quality block splitting makes compression 2.1% better for usual approach and 1.8% better for approach with reuse of artifacts. However, it’s about 3-5 times slower.
*   Optimizations in high quality block splitting allows us to achieve 1.4-2 times faster compression with a drop in quality of 0.25% for Brotli 9 and 0.4% for Brotli 5 which is acceptable.
*   With a raise in removal ratio, compression rate decreases for all the algorithms (including usual Brotli). That happens due to the decreasing size of files. However, the more we delete, the smaller the improvement we will get with our approach, since with big removals there will be less backward references we can actually reuse. 
*   One can notice that for all the variants compression rates for Brotli 5 and Brotli 9 are almost the same yet Brotli 5 works much faster. Therefore it’s better to consider applying this approach with Brotli 5 compression levels.

The best results for our use-case were achieved with those two options:


*   Brotli 5 with reuse of backward references and block splits for literals and commands with usual block splitting algorithm.
*   Brotli 5 with reuse of backward references and block splits for literals and commands with optimized high quality block splitting algorithm.

![](https://snipboard.io/XqWCjv.jpg)

For better understanding here are plots for compression rates and compression speeds for some of the experiments described above:

![](https://snipboard.io/NYxaQM.jpg)

![](https://snipboard.io/QOamtS.jpg)

# **Other experiments**

Backward references reuse for Brotli 10, 11.

Brotli 10 and 11 use an advanced iterative algorithm for backward reference search called zopflification. 

All the different backward references for the text can be represented as a graph. The shortest path from the beginning to the end in this graph is a representation of copying decisions made for the text and can form a set of backward references to use. 

However, the shortest path will try to use copying of the biggest substrings possible, which  may not be optimal because of Huffman coding applied after, as sometimes it’s better to have a lot of backward references which have just few unique values of copy lengths and copy distances than few backward references with large copy lengths. That way the overall encoding will be shorter. 

Zopflification applies an iterative algorithm that finds a good path in the graph (probably not optimal but good enough) which is better than the shortest one in terms of compression.

The approach we tried is to reduce the amount of edges (backward references) in this graph. We forced zopflification not to use any other backward references except the ones that are presented in the backward references array from the decoder. Having such a graph, we can then apply an iterative zopflification process which takes significantly less CPU time on calculations and results in almost the same quality of final backward references.

The results for this approach were only calculated for the case when we try to recompress exactly the same file as was compressed before (and decided not to move forward with it and haven’t implemented it further).

![](https://snipboard.io/hpARPF.jpg)


Decreasing the amount of edges in the graph helps to make compression 1.6  times faster and 1.67% worse for Brotli 10 and 1.3 times  faster with no drop in quality for Brotli 11, although still quite far for the on-the-fly compression.

However, there is still a space for more improvements here.


# **Further work**



*   Design and implement the approach for the case when a new file is obtained by adding something to the original file.

Additionally, when a new file is a set of different insert and delete transformations.



*   Reuse some other artifacts. 

As examples, we can collect entropy coding decisions during decoding which can be used in zopflification or histogram clustering to make them faster.

*   For references to the static dictionary, additionally store a _dictionary_ _word_ in the decoder, so there will be no need to calculate it in the encoder while checking a reference.
*   Do a security review. This aspect hasn’t been researched and tested properly.


