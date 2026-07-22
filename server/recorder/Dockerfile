FROM python:3.12-alpine

WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY protocol.py recorder.py ./

CMD ["python", "-u", "recorder.py"]
