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
First, the goal will be to process heavier and heavier compression technique in order.
We must compress as much as possible pixels as we can 'easily' ( = smallest bpp ).
When it is not possible anymore then switch to another technique.

They MUST FOLLOW THE ORDER as described here, to be the most efficient in term of compression and do not annoy each other.

## First Technique : Tile RGB gradient

Analyze the image and find tile of 16x16,8x16,16x8,8x8,4x8,8x4 and finally 4x4 tile of RGB gradients. (PrepareQuadSmooth, FittingQuadSmooth functions)
We do it in descending order to maximize the cover surface at the cheapest cost first, then increase the cost per pixel (bit per pixel).
Gradient analysis perform a very efficient encoding scheme :
- It maps correlation between the full 3 plane (RGB).
- It cost only a single color per tile in average ( + pixel on the borders )
- By using bigger tile first, we reduce the amount of pixel stream.

Each tile found is removed from the list of tile to process.

## Second Technique : 3D LUT

Same as the first technique, we still want to extract 'more' compression from the correlation between RGB layers.
In GPU, technique generally rely on having partition inside a tile of data.
Each partition handle a palette of color that is synthetized between two color linearly.
While such scheme is quite efficient to get something initially running, it is so because it is easy to implement in hardware.

Here in software we have the luxury to possibly use big look-up-table (LUT) because we have memory available at runtime.
Computation in those LUT can resume very complex calculation that runtime hardware texture decoder can't do easily.

Software compressor focus generally on using some kind of frequency domain (DCT, Fourier Transform, Wavelet Transform),
cut the less important frequencies and repack the thing.

Such approach do generate noise that I want to avoid and prefer to take my 'losses' somewhere else.
(I point to the obvious that at some point, whatever how good the compression is, you will have to loose some kind of information or you will be stuck).

So the second technique, is the ability to create a MULTI-COLOR GRADIENT from ONLY TWO COLORS.

For each tile :
- Minimum RGB and maximum RGB are extracted. They give the corner of a bounding box.
	Min(R,G,B) and Max(R,G,B)
	Because we use min/max, it automatically garantees that ANY color in the tile is ALWAYS inside the bounding box.
	
- The bounding box is normalized into a UNIT CUBE.
	Diff(R,G,B) = Max(R,G,B) - Min(R,G,B)
	Then normalize.
	
- That unit cube is splitted into a 64x64x64 array. All the color in the tile point are remapped to a position somewhere in the unit cube.
	(X,Y,Z) (range 0..63 for each)
	
- After that, we look up X,Y,Z entry inside a 3D LUT.
	The LUT gives us the distance from the closest available color in the LUT, and its index in 4 bit,5 bit,6 bit (t value).
	We do the sum of distance (score to find how the current tile compare to the LUT distance field).
	The best match is selected.
	
	Note that each 3D LUT is tested with 48 patterns for best score :
	- Flip X Axis, Flip Y Axis, Flip Z Axis (8 patterns)
	- Swap X,Y,Z axis (6 patterns)
	
	Once the best LUT matches, we use the bit index using another 3x 1D LUT to find back a normalized X,Y,Z. (Table return unsigned 9 bit  0..256 in encoder, decoder will probably use 0..128).
		X[t value]
		Y[t value]
		Z[t value]
	
	We can recompute back the decompressed RGB value from the bounding box and normalized coordinates.
		R = MinR + ((MaxR - MinR)*X[t value]) / 256
		G = MinG + ((MaxG - MinG)*Y[t value]) / 256
		B = MinB + ((MaxB - MinB)*Z[t value]) / 256

	After that, RGB distance is tested against original RGB input. If value are too far away, it is rejected.
	
	Same, if tile is accepted, reject from list of tile available for compression.

	Uncompressed, in 4 bit mode for 8x8 tile, BPP cost is 16+24+24+1 (Header) + 4*64 = 5.01 bpp.

## Thirst Technique : Palette

	[UNEXPLORED YET] : There could be cases when tile UNIQUE COLOR COUNT <= 8 ( or 16 ) and it could be encoded using a palette (676 ? 777 ? 888 ?)
	It may not be encodable by 3D Lut due to the amount of color variation.
	It could be a technique to save tile information.
	Obviously, it is 'heavy' stuff. Cost per pixel is for 8 color palette : 3x64 + 24*8 = 6 Bit per pixel. + Tile type need to be saved (~6.25 bpp).  
	
	It is an interesting way still process 3 planes with a single information per pixel.
	Palette compression, color reduction technique could be applied there too.
	
## Fourth Technique : Same again... in 2D this time !

	We have explorered as much as possible interaction between the 3 planes : R,G,B.
	We now must explore planes seperately. Still, there are some information we do not want to go wasted :
	
	- Gradient may happen only in RG/GB/RB plane, failed in 3D. But we can try to ignore the plane that made the gradient fail before and see
	if we find new gradient only occuring in 2 planes.
	
	- Once we applied all gradient, we can use the same technique as the 3D LUT, but this time in 2D.
		- Bounding box in 2D. -> Unit Square
		- Remap 2D color to unit square and look up nearest color available.
		- Note that 2D LUT have to be tested against 8 Patterns in 2D (compare to 48 in 3D):
			- Flip X
			- Flip Y
			- Swap X/Y
			
	[TODO : Do we 1/2 or 1/4 CoCg, do we switch our system to allow better compression)

## Fifth Technique : Same again... in 1D this time !
	Gradient could still be there for seperate plane.
	
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
