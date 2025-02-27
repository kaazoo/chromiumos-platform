// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// TODO(greenjustin): This entire file should be replaced with LibYUV.
use byteorder::ByteOrder;
use byteorder::LittleEndian;

/// Copies `src` into `dst` as NV12, handling padding.
pub fn nv12_copy(
    src_y: &[u8],
    src_y_stride: usize,
    dst_y: &mut [u8],
    dst_y_stride: usize,
    src_uv: &[u8],
    src_uv_stride: usize,
    dst_uv: &mut [u8],
    dst_uv_stride: usize,
    width: usize,
    height: usize,
) {
    for y in 0..height {
        dst_y[(y * dst_y_stride)..(y * dst_y_stride + width)]
            .copy_from_slice(&src_y[(y * src_y_stride)..(y * src_y_stride + width)]);
    }
    for y in 0..(height / 2) {
        dst_uv[(y * dst_uv_stride)..(y * dst_uv_stride + width)]
            .copy_from_slice(&src_uv[(y * src_uv_stride)..(y * src_uv_stride + width)]);
    }
}

/// Replace 0 padding with the last pixels of the real image. This helps reduce compression
/// artifacts caused by the sharp transition between real image data and 0.
pub fn extend_border_nv12(
    y_plane: &mut [u8],
    uv_plane: &mut [u8],
    visible_width: usize,
    visible_height: usize,
    coded_width: usize,
    coded_height: usize,
) {
    assert!(visible_width > 1);
    assert!(visible_height > 1);
    for y in 0..visible_height {
        let row_start = y * coded_width;
        for x in visible_width..coded_width {
            y_plane[row_start + x] = y_plane[row_start + x - 1]
        }
    }
    for y in visible_height..coded_height {
        let (src, dst) = y_plane.split_at_mut(y * coded_width);
        dst[0..coded_width].copy_from_slice(&src[((y - 1) * coded_width)..(y * coded_width)]);
    }
    for y in 0..(visible_height / 2) {
        let row_start = y * coded_width;
        for x in visible_width..coded_width {
            // We use minus 2 here because we want to actually repeat the last 2 UV values.
            uv_plane[row_start + x] = uv_plane[row_start + x - 2]
        }
    }
    for y in (visible_height / 2)..(coded_height / 2) {
        let (src, dst) = uv_plane.split_at_mut(y * coded_width);
        dst[0..coded_width].copy_from_slice(&src[((y - 1) * coded_width)..(y * coded_width)]);
    }
}

/// Copies `src` into `dst` as I4xx (YUV tri-planar).
///
/// This function does not change the data layout beyond removing any padding in the source, i.e.
/// both `src` and `dst` are 3-planar YUV buffers.
///
/// `strides` and `offsets` give the stride and starting position of each plane in `src`. In `dst`
/// each plane will be put sequentially one after the other.
///
/// `sub_h` and `sub_v` enable horizontal and vertical sub-sampling, respectively. E.g, if both
/// `sub_h` and `sub_v` are `true` the data will be `4:2:0`, if only `sub_v` is `true` then it will be
/// `4:2:2`, and if both are `false` then we have `4:4:4`.
pub fn i4xx_copy(
    src: &[u8],
    dst: &mut [u8],
    width: usize,
    height: usize,
    strides: [usize; 3],
    offsets: [usize; 3],
    (sub_h, sub_v): (bool, bool),
) {
    // Align width and height of UV planes to 2 if sub-sampling is used.
    let uv_width = if sub_h { (width + 1) / 2 } else { width };
    let uv_height = if sub_v { (height + 1) / 2 } else { height };

    let dst_y_size = width * height;
    let dst_u_size = uv_width * uv_height;
    let (dst_y_plane, dst_uv_planes) = dst.split_at_mut(dst_y_size);
    let (dst_u_plane, dst_v_plane) = dst_uv_planes.split_at_mut(dst_u_size);

    // Copy Y.
    let src_y_lines = src[offsets[0]..]
        .chunks(strides[0])
        .map(|line| &line[..width]);
    let dst_y_lines = dst_y_plane.chunks_mut(width);
    for (src_line, dst_line) in src_y_lines.zip(dst_y_lines).take(height) {
        dst_line.copy_from_slice(src_line);
    }

    // Copy U.
    let src_u_lines = src[offsets[1]..]
        .chunks(strides[1])
        .map(|line| &line[..uv_width]);
    let dst_u_lines = dst_u_plane.chunks_mut(uv_width);
    for (src_line, dst_line) in src_u_lines.zip(dst_u_lines).take(uv_height) {
        dst_line.copy_from_slice(src_line);
    }

    // Copy V.
    let src_v_lines = src[offsets[2]..]
        .chunks(strides[2])
        .map(|line| &line[..uv_width]);
    let dst_v_lines = dst_v_plane.chunks_mut(uv_width);
    for (src_line, dst_line) in src_v_lines.zip(dst_v_lines).take(uv_height) {
        dst_line.copy_from_slice(src_line);
    }
}

/// Copies `src` into `dst` as I410, removing all padding and changing the layout from packed to
/// triplanar. Also drops the alpha channel.
pub fn y410_to_i410(
    src: &[u8],
    dst: &mut [u8],
    width: usize,
    height: usize,
    strides: [usize; 3],
    offsets: [usize; 3],
) {
    let src_lines = src[offsets[0]..]
        .chunks(strides[0])
        .map(|line| &line[..width * 4]);

    let dst_y_size = width * 2 * height;
    let dst_u_size = width * 2 * height;

    let (dst_y_plane, dst_uv_planes) = dst.split_at_mut(dst_y_size);
    let (dst_u_plane, dst_v_plane) = dst_uv_planes.split_at_mut(dst_u_size);
    let dst_y_lines = dst_y_plane.chunks_mut(width * 2);
    let dst_u_lines = dst_u_plane.chunks_mut(width * 2);
    let dst_v_lines = dst_v_plane.chunks_mut(width * 2);

    for (src_line, (dst_y_line, (dst_u_line, dst_v_line))) in src_lines
        .zip(dst_y_lines.zip(dst_u_lines.zip(dst_v_lines)))
        .take(height)
    {
        for (src, (dst_y, (dst_u, dst_v))) in src_line.chunks(4).zip(
            dst_y_line
                .chunks_mut(2)
                .zip(dst_u_line.chunks_mut(2).zip(dst_v_line.chunks_mut(2))),
        ) {
            let y = LittleEndian::read_u16(&[src[1] >> 2 | src[2] << 6, src[2] >> 2 & 0b11]);
            let u = LittleEndian::read_u16(&[src[0], src[1] & 0b11]);
            let v = LittleEndian::read_u16(&[src[2] >> 4 | src[3] << 4, src[3] >> 4 & 0b11]);
            LittleEndian::write_u16(dst_y, y);
            LittleEndian::write_u16(dst_u, u);
            LittleEndian::write_u16(dst_v, v);
        }
    }
}

/// Simple implementation of MM21 to NV12 detiling. Note that this Rust-only implementation is
/// unlikely to be fast enough for production code, and is for testing purposes only.
/// TODO(b:380280455): We will want to speed this up and also add MT2T support.
pub fn detile_plane(
    src: &[u8],
    dst: &mut [u8],
    width: usize,
    height: usize,
    tile_width: usize,
    tile_height: usize,
) -> Result<(), String> {
    if width % tile_width != 0 || height % tile_height != 0 {
        return Err("Buffers must be aligned to tile dimensions for detiling".to_owned());
    }

    let tile_size = tile_width * tile_height;
    let mut output_idx = 0;
    for y_start in (0..height).step_by(tile_height) {
        let tile_row_start = y_start * width;
        for y in 0..tile_height {
            let row_start = tile_row_start + y * tile_width;
            for x in (0..width).step_by(tile_width) {
                let input_idx = row_start + x / tile_width * tile_size;
                dst[output_idx..(output_idx + tile_width)]
                    .copy_from_slice(&src[input_idx..(input_idx + tile_width)]);
                output_idx += tile_width;
            }
        }
    }

    Ok(())
}

pub fn mm21_to_nv12(
    src_y: &[u8],
    dst_y: &mut [u8],
    src_uv: &[u8],
    dst_uv: &mut [u8],
    width: usize,
    height: usize,
) -> Result<(), String> {
    let y_tile_width = 16;
    let y_tile_height = 32;
    detile_plane(src_y, dst_y, width, height, y_tile_width, y_tile_height)?;
    detile_plane(
        src_uv,
        dst_uv,
        width,
        height / 2,
        y_tile_width,
        y_tile_height / 2,
    )
}

/// Simple implementation of NV12 to I420. Again, probably not fast enough for production, should
/// TODO(b:380280455): We may want to speed this up.
pub fn nv12_to_i420_chroma(src_uv: &[u8], dst_u: &mut [u8], dst_v: &mut [u8]) {
    for i in 0..src_uv.len() {
        if i % 2 == 0 {
            dst_u[i / 2] = src_uv[i];
        } else {
            dst_v[i / 2] = src_uv[i];
        }
    }
}

pub fn nv12_to_i420(
    src_y: &[u8],
    dst_y: &mut [u8],
    src_uv: &[u8],
    dst_u: &mut [u8],
    dst_v: &mut [u8],
) {
    dst_y.copy_from_slice(src_y);
    nv12_to_i420_chroma(src_uv, dst_u, dst_v);
}

pub fn i420_to_nv12_chroma(src_u: &[u8], src_v: &[u8], dst_uv: &mut [u8]) {
    for i in 0..dst_uv.len() {
        if i % 2 == 0 {
            dst_uv[i] = src_u[i / 2];
        } else {
            dst_uv[i] = src_v[i / 2];
        }
    }
}

pub fn i420_to_nv12(src_y: &[u8], dst_y: &mut [u8], src_u: &[u8], src_v: &[u8], dst_uv: &mut [u8]) {
    dst_y.copy_from_slice(src_y);
    i420_to_nv12_chroma(src_u, src_v, dst_uv);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_mm21_to_nv12() {
        let test_input = include_bytes!("test_data/puppets-480x270_20230825.mm21.yuv");
        let test_expected_output = include_bytes!("test_data/puppets-480x270_20230825.nv12.yuv");

        let mut test_output = [0u8; 480 * 288 * 3 / 2];
        let (test_y_output, test_uv_output) = test_output.split_at_mut(480 * 288);
        mm21_to_nv12(
            &test_input[0..480 * 288],
            test_y_output,
            &test_input[480 * 288..480 * 288 * 3 / 2],
            test_uv_output,
            480,
            288,
        )
        .expect("Failed to detile!");
        assert_eq!(test_output, *test_expected_output);
    }

    #[test]
    fn test_nv12_to_i420() {
        let test_input = include_bytes!("test_data/puppets-480x270_20230825.nv12.yuv");
        let test_expected_output = include_bytes!("test_data/puppets-480x270_20230825.i420.yuv");

        let mut test_output = [0u8; 480 * 288 * 3 / 2];
        let (test_y_output, test_uv_output) = test_output.split_at_mut(480 * 288);
        let (test_u_output, test_v_output) = test_uv_output.split_at_mut(480 * 288 / 4);
        nv12_to_i420(
            &test_input[0..480 * 288],
            test_y_output,
            &test_input[480 * 288..480 * 288 * 3 / 2],
            test_u_output,
            test_v_output,
        );
        assert_eq!(test_output, *test_expected_output);
    }

    #[test]
    fn test_i420_to_nv12() {
        let test_input = include_bytes!("test_data/puppets-480x270_20230825.i420.yuv");
        let test_expected_output = include_bytes!("test_data/puppets-480x270_20230825.nv12.yuv");

        let mut test_output = [0u8; 480 * 288 * 3 / 2];
        let (test_y_output, test_uv_output) = test_output.split_at_mut(480 * 288);
        i420_to_nv12(
            &test_input[0..(480 * 288)],
            test_y_output,
            &test_input[(480 * 288)..(480 * 288 * 5 / 4)],
            &test_input[(480 * 288 * 5 / 4)..(480 * 288 * 3 / 2)],
            test_uv_output,
        );
        assert_eq!(test_output, *test_expected_output);
    }
}
