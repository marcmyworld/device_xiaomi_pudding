# Device Tree for Xiaomi 17 (pudding)

<p align="center">
  <img src="./docs/pudding.png" alt="Xiaomi 17" width="420">
</p>
<p align="center"><sub>Xiaomi 17 (source: Xiaomi)</sub></p>

## Spec Sheet

| Feature                 | Specification                                                              |
| :---------------------- | :--------------------------------                                          |
| CPU                     | 2x4.6 GHz Oryon V3 Phoenix L + 6x3.62 GHz Oryon V3 Phoenix M               |
| Chipset                 | Qualcomm SM8850-AC Snapdragon 8 Elite Gen 5 (3 nm)                         |
| GPU                     | Adreno 840                                                                 |
| Memory                  | 12 GB / 16 GB LPDDR5X                                                      |
| Shipped Software        | Android 16, HyperOS 3                                                      |
| Storage                 | 256 GB / 512 GB / 1 TB UFS 4.1                                             |
| Battery                 | Si/C Li-Ion: 6330mAh (Global) or 7000mAh (China)                           |
| Dimensions              | 151.1 x 71.8 x 8.1 mm (5.95 x 2.83 x 0.32 in)                              |
| Display                 | 6.3 inches, LTPO AMOLED, 120Hz, 1220 x 2656 pixels, ~460ppi                |
| Rear Camera             | 50 MP, f/1.7, 23mm (wide), 1.31/1", 1.2µm, dual pixel PDAF, OIS          |
|                         | 50 MP, f/2.0, 60mm (telephoto), 1/2.76", PDAF (10cm - ∞), OIS, 2.6x zoom   |
|                         | 50 MP, f/2.4, 17mm, 102˚ (ultrawide), 1/2.88", 0.61µm                     |
| Front Camera            | 50 MP, f/2.2, 21mm (wide), AF                                              |
| Release Date            | September 2025 (China)                                                     |

## Building LineageOS

Clone the device manifest into `.repo/local_manifests/`:
```bash
git clone https://github.com/xiaomi17-pudding-dev/local_manifests_pudding .repo/local_manifests/pudding.xml
```

Initialize the build environment and start compilation:

```bash
source build/envsetup.sh
lunch lineage_pudding-user
m bacon
```

Or use the build command specified by your rom.

## Generating the vendor trees

1. **Extract Blobs:** Run the extract script pointing to the unpacked stock dump:
   ```bash
   ./extract-files.py <path-to-unpacked-firmware>
   ```

2. Your extracted blobs are saved within a vendor tree located at `vendor/xiaomi/pudding`.

## Credits
Initially generated with [aospdtgen](https://github.com/SebaUbuntu/aospdtgen).

## License
Apache-2.0. Proprietary files are redistributed as shipped in the stock firmware above.
