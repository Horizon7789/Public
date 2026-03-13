import sys
from textblob import TextBlob

def tag_sentence(text):
    blob = TextBlob(text)
    # Returns: Word|POS Word|POS
    return " ".join([f"{word}|{pos}" for word, pos in blob.tags])

if __name__ == "__main__":
    if len(sys.argv) > 1:
        print(tag_sentence(" ".join(sys.argv[1:])))
