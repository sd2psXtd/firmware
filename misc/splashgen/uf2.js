
// UF2:
const UF2_MAGIC_START0 = 0x0A324655;
const UF2_MAGIC_START1 = 0x9E5D5157;
const UF2_MAGIC_END = 0x0AB16F30;
const UF2_BLOCK_SIZE = 512;
const UF2_PAYLOAD_SIZE = 256;
const UF2_FAMILY_ID = 0xe48bff56;
const UF2_BASE_ADDR = 0x10800000;
const MAX_FILE_SIZE = 16576512;


function payloadToUf2(arrayBuffer) {
    // Add a terminating 0x00 after the end of the arrayBuffer
    const origData = new Uint8Array(arrayBuffer);
    const data = new Uint8Array(origData.length + 1);
    data.set(origData, 0);
    data[data.length - 1] = 0x00;

    const numBlocks = Math.ceil(data.length / UF2_PAYLOAD_SIZE);
    const uf2 = new Uint8Array(numBlocks * UF2_BLOCK_SIZE);

    for (let blockNum = 0; blockNum < numBlocks; ++blockNum) {
        const ptr = blockNum * UF2_BLOCK_SIZE;
        const payload = data.subarray(blockNum * UF2_PAYLOAD_SIZE, (blockNum + 1) * UF2_PAYLOAD_SIZE);

        const view = new DataView(uf2.buffer, ptr, UF2_BLOCK_SIZE);
        view.setUint32(0, UF2_MAGIC_START0, true);
        view.setUint32(4, UF2_MAGIC_START1, true);
        view.setUint32(8, 0x00002000, true);
        view.setUint32(12, UF2_BASE_ADDR + blockNum * UF2_PAYLOAD_SIZE, true);
        view.setUint32(16, UF2_PAYLOAD_SIZE, true);
        view.setUint32(20, blockNum, true);
        view.setUint32(24, numBlocks, true);
        view.setUint32(28, UF2_FAMILY_ID, true);
        uf2.set(payload, ptr + 32);
        for (let i = payload.length; i < UF2_PAYLOAD_SIZE; ++i) {
            uf2[ptr + 32 + i] = 0;
        }
        view.setUint32(512 - 4, UF2_MAGIC_END, true);
    }
    return uf2;
}

function mergeUf2Files(uf2_1, uf2_2) {
    const totalSize = uf2_1.byteLength + uf2_2.byteLength;
    const mergedUf2 = new Uint8Array(totalSize);
    mergedUf2.set(new Uint8Array(uf2_1), 0);
    mergedUf2.set(new Uint8Array(uf2_2), uf2_1.byteLength);

    const numBlocks = Math.ceil(mergedUf2.length / UF2_BLOCK_SIZE);

    for (let blockNum = 0; blockNum < numBlocks; ++blockNum) {
        const ptr = blockNum * UF2_BLOCK_SIZE;

        const view = new DataView(mergedUf2.buffer, ptr, UF2_BLOCK_SIZE);
        view.setUint32(20, blockNum, true);
        view.setUint32(24, numBlocks, true);
    }

    return mergedUf2;
}
