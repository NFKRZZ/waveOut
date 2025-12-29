import sys
import demucs.separate
import torch

def main():
    if len(sys.argv) < 2:
        print("Usage: python demucs_script.py <audio_file>")
        sys.exit(1)
    
    audio_file = sys.argv[1]
    
    print(f"CUDA available: {torch.cuda.is_available()}")
    print(f"CUDA device: {torch.cuda.get_device_name(0) if torch.cuda.is_available() else 'None'}")
    
    demucs.separate.main(["-n", "htdemucs_ft", "--device", "cuda", audio_file])

if __name__ == "__main__":
    main()