#include "drc_wrap.h"

int readParametersFromFile(SndDrcMemory *mem, const char* filename) {
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        printf("Failed to open the file.\n");
        return -1;
    }

    // Read the parameters from the file
    char line[256];
    char param[256];
    float value;

    while (fgets(line, sizeof(line), file) != NULL) {
        if (sscanf(line, "%s = %f;", param, &value) == 2) {
            if (strcmp(param, "pregain") == 0) {
                mem->pregain = value;
            } else if (strcmp(param, "threshold") == 0) {
                mem->threshold = value;
            } else if (strcmp(param, "knee") == 0) {
                mem->knee = value;
            } else if (strcmp(param, "ratio") == 0) {
                mem->ratio = value;
            } else if (strcmp(param, "attack") == 0) {
                mem->attack = value;
            } else if (strcmp(param, "release") == 0) {
                mem->release = value;
            } else if (strcmp(param, "predelay") == 0) {
                mem->predelay = value;
            } else if (strcmp(param, "releasezone1") == 0) {
                mem->releasezone1 = value;
            } else if (strcmp(param, "releasezone2") == 0) {
                mem->releasezone2 = value;
            } else if (strcmp(param, "releasezone3") == 0) {
                mem->releasezone3 = value;
            } else if (strcmp(param, "releasezone4") == 0) {
                mem->releasezone4 = value;
            } else if (strcmp(param, "postgain") == 0) {
                mem->postgain = value;
            } else if (strcmp(param, "wet") == 0) {
                mem->wet = value;
            }
        }
    }

    fclose(file);
    return 0;
}

void snd_drc_init(SndDrcMemory *mem, const char *filePath, int sampleRate) {
    readParametersFromFile(mem, filePath);

    sf_advancecomp(&(mem->state), sampleRate,
                mem->pregain, mem->threshold, mem->knee, mem->ratio, mem->attack, mem->release, mem->predelay,
                mem->releasezone1, mem->releasezone2, mem->releasezone3, mem->releasezone4, mem->postgain, mem->wet);

    for (int i = 0; i < SF_COMPRESSOR_SPU; i++) {
        mem->inBuf[2 * i] = 0;
        mem->inBuf[2 * i + 1] = 0;
        mem->outBuf[2 * i] = 0;
        mem->outBuf[2 * i + 1] = 0;
    }
    mem->inIdx = 0;
    mem->outIdx = 0;
}

void snd_drc_process(SndDrcMemory *mem, int samplesPerChannels, float *in, float *out) {
	// note that the compressor does not output one sample per input sample, because the compressor
	// works in subchunks of 32 samples (this is defined via SF_COMPRESSOR_SPU in compressor.c)

    int totalSamples = samplesPerChannels + mem->inIdx;
    int processSamples = floor(totalSamples / SF_COMPRESSOR_SPU) * SF_COMPRESSOR_SPU;
    int leftSamples = totalSamples - processSamples;        //  alway smaller than SF_COMPRESSOR_SPU (32)
    int snd_idx, io_idx, i;

    //  buffer is smaller than SF_COMPRESSOR_SPU
    if (processSamples < SF_COMPRESSOR_SPU) {
        //  in buffer <- in
        for (mem->inIdx, io_idx = 0; mem->inIdx < totalSamples; mem->inIdx++, io_idx++) {
            mem->inBuf[2 * mem->inIdx] = in[2 * io_idx];
            mem->inBuf[2 * mem->inIdx + 1] = in[2 * io_idx + 1];
        }
        //  out <- out buffer
        for (io_idx = 0; io_idx < samplesPerChannels; io_idx++) {
            out[2 * io_idx] = mem->outBuf[2 * io_idx];
            out[2 * io_idx + 1] = mem->outBuf[2 * io_idx + 1];
        }
        //  buffer queueing
        for (i = 0, io_idx; io_idx < mem->outIdx; i++, io_idx++) {
            mem->outBuf[2 * i] = mem->outBuf[2 * io_idx];
            mem->outBuf[2 * i + 1] = mem->outBuf[2 * io_idx + 1];
        }
        mem->outIdx = i;

        return;
    }

    sf_snd input_snd = sf_snd_new(processSamples, mem->sampleRate, true);
    sf_snd output_snd = sf_snd_new(processSamples, mem->sampleRate, true);

    //  snd <- buffered samples
    for (i = 0, snd_idx = 0; i < mem->inIdx; i++, snd_idx++) {
        input_snd->samples[snd_idx].L = mem->inBuf[2 * i];
        input_snd->samples[snd_idx].R = mem->inBuf[2 * i + 1];
    }

    //  snd <- new samples
    for(snd_idx, io_idx = 0; snd_idx < processSamples; snd_idx++, io_idx++) {
        input_snd->samples[snd_idx].L = in[2 * io_idx];
        input_snd->samples[snd_idx].R = in[2 * io_idx + 1];
    }

    //  in buffer <- remained samples
    mem->inIdx = 0;
    for (mem->inIdx = 0, io_idx; io_idx < samplesPerChannels; mem->inIdx++, io_idx++) {
        mem->inBuf[2 * mem->inIdx] = in[2 * io_idx];
        mem->inBuf[2 * mem->inIdx + 1] = in[2 * io_idx + 1];
    }

    sf_compressor_process(&(mem->state), input_snd->size, input_snd->samples, output_snd->samples);

    //  out <- processed remain
    for (i = 0, io_idx = 0; i < mem->outIdx; i++, io_idx++) {
        out[2 * io_idx] = mem->outBuf[2 * i];
        out[2 * io_idx + 1] = mem->outBuf[2 * i + 1];
    }

    //  out <- processed
    for (io_idx, snd_idx = 0; io_idx < samplesPerChannels; io_idx++, snd_idx++) {
        out[2 * io_idx] = output_snd->samples[snd_idx].L;
        out[2 * io_idx + 1] = output_snd->samples[snd_idx].R;
    }

    //  out buffer <- processed remain samples
    for (mem->outIdx = 0, snd_idx; snd_idx < processSamples; mem->outIdx++, snd_idx++) {
        mem->outBuf[2 * mem->outIdx] = output_snd->samples[snd_idx].L;
        mem->outBuf[2 * mem->outIdx + 1] = output_snd->samples[snd_idx].R;
    }

    sf_snd_free(input_snd);
    sf_snd_free(output_snd);
}