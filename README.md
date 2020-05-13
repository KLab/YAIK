# YAIK
Research Framework for image compression.
(Y)et (A)nother (I)mage (K)compression

# Goal

Try alternative techniques to compress images. The reason for it, is that Jpeg / Webp are going into the 'frequency' transform to remove information. 
Depending on the technique, that results in unwanted noise or too much flattened surfaces.
So the plan is to try different compression technique that differs from frequency domain compression.
GPU offers quite good compression technique, by encoding block and compressing them with other schemes (we are going to detail them later) but are limited by transistor budget and decompression scheme.

Anyway, that is the goal of the project : Research some compression algorithm. Especially some that could be good for our games.
Trying to keep as much as possible quality from 'anime' like illustrations, with obviously minimum file size AND fast decoding time if possible.

# Rationale
	
Obviously, hardware based compression format can themselves be restructured to use a different pre-encoding and be compressed with a lossless compressor, and decompressed into a GPU friendly format.

So I am going to ignore that but just really focus here about the CORE difference between a software and hardware decoder.
See the pionnering work done by [Rich Geldreich](https://twitter.com/richgel999) for [Crunch](https://github.com/BinomialLLC/crunch) (and later on the [BasisU](https://github.com/BinomialLLC/basis_universal))  

Software decoder have an advantage over hardware based decoded that they can have more convoluted decompression scheme that are not affordable in hardware :
- Bigger lookup table (even if cache miss locality is well preserved)
- Variable size of item in surface. (All block based format are keeping a strict tile format, does not dynamically change within an image)
- Completly different algorithm based on tile type.

Thus, bringing more quality per bit spent or bringing higher compression rate than hardware format seems possible in theory.
After that, techniques as those seen in Crunch or BasisU can probably be applied to compress even more.

Of course, the first basic idea is to use modern lossless compressor like ZStd (it is the default compressor I am using for the project) on the top of that and make the data even smaller.

There are various parameters for the compression/decompression :
- Compression time : does the encoding requires a huge amount of time ? Can it uses some kind of heuristics to converge rapidly toward a good enough solution ?
- Quality of the compression : Can we reach high quality if we allow more bits ? What is the quality of the image per bit spent ?
- Decompression time.
- Runtime memory usage at decompression.
- Runtime memory usage at runtime (ie compressed hardware texture format are smaller as they are used as is by the hardware).

# Code Structure

There are three parts.
- The encoder project.
- The decoder project.
- The common files.

## The Encoder

The encoder code is for now a research project. Not something ready for production.
I did not really care if there are leak. I just wanted to be able to throw some data in, do some math and check
if the technique I plan to do works or not.

### The framework

There is :
- the 'Plane' class : it is a 2D array of value (32 bit signed integer). It provides various operations on planes.
- the 'Image' class : it is a collection of 3 or 4 planes. And it provides also some operations on it.
- the 'EncoderContext' which store the various Plane and Image during the encoding.

### The encoder context.

The context provides the following processing for now :
- When alpha channel is used, due to possible mipmappping of the texture, analyze the image and build a small 1 bit bitmap telling which tile of RGB pixels can be rejected (value can be garbage at decompression) because alpha is zero.
  This file format is not about storing any information but a visual RGBA image. So if alpha is ZERO, and in area that is not needed, even with mipmapping the texture, we decide to completly avoid RGB value from the stream.
- Compress the alpha channel information.
- Analyze the image in tile of 8x8 and 4x4 to find bilinear color gradient inside an image.
- Analyze tile of 8x8 and compress them using range compression of 4 bit or 3 bit per pixel.

## The Decoder

For now, decoder is not updated to the latest state of the encoder but will be soon. I did not want to publish a decoder that did not match the encoder.

## The common files.

The only stuff I am very strict about is the /include folder. Those files are to be maintained like it was 'Terminator' doing the job.
The header must be perfect, with no waste. Or at least tries to.
It is necessary to keep the API cleans, file format clean, and decoder which is the important part for the library user easy to use and readable.
It also reflect the quality of the decoder implementation.

## Libraries

For now, [ZStd](https://github.com/facebook/zstd) is used for the encoder and decoder.
[std_image](https://github.com/nothings/stb) is used for PNG reading and writing in the encoder (decoder for debug purpose only, not in production build)

## Default Project

A Visual Studio 2019 project is available inside encoder\vc_prj. Should build and compile straight out of the box.

# Encoding techniques

Some techniques have been implemented and failed. The source is still inside the project but will not be introduced.

## Tile gradient

Analyze the image and find tile of 4x4 and 8x8 of RGB gradients. (PrepareQuadSmooth, FittingQuadSmooth functions)

## Range Compression

Analyze a single plane tile of 8x8 and try to find a LUT table that encode the tile the best using only 3 or 4 bit per pixels (DynamicTileEncode).

## Correlated YCoCg (RGB ?)

Not implemented yet. It is the technique used in HW encoder but at a simpler level. It interpolates TWO or THEE colors.
My idea would be to not only fit along a 3D segment (Start-End color point) but use more convoluted shapes (like splines or multiple segments).

## Correlated CoCg

Same as the previous technique, except that we correlate only the CoCg planes in 2D and try to fit along a 'shape' to match the change.
We allow the Y plane to modulate the color has needed but do not try to interfere with it.
	
# Decoder

TODO LATER when decoder source code will be uploaded.
