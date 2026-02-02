#!/usr/bin/env python3
# /// script
# dependencies = [
#     "typer>=0.9.0",
# ]
# ///

import typer
from pathlib import Path

app = typer.Typer()

@app.command()
def hello(input: Path):
    typer.echo(f"Hello {name}!")

if __name__ == "__main__":
    app()